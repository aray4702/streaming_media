---
issue: 120
title: RTX payload padding as libwebrtc (GeneratePadding, GetPayloadPaddingPacket), behind pacer v2 + rtx v2
severity: sev:2
status: fixed
area: area:loss, area:cc
regression_tests: testbench/scenarios/issue_120_rtx_payload_padding.scn, testbench/scenarios/issue_120_rtx_payload_padding_capacity_drop.scn, tests/unit/rtp/rtp_sender_test.cc, tests/unit/cc/pacing_controller_test.cc
---
<!-- sm-agent: fixer -->

# #120: RTX payload padding as libwebrtc

## Issue

On `issue_23_handover` with `pacer v2` and `rtx v2` the sender put 9.6-15 kbps of empty padding on the link where
libwebrtc sends 0.6 kbps, because libwebrtc's padding is mostly RTX *payload* padding (a resend of the newest large
packet). Found while working the flip in #116 (PR #116 comment of 13:23 UTC) and placed by the Owner on #120:
https://github.com/aray4702/streaming_media/issues/120.

## Root cause

`PacingController` queued fixed 224 B placeholders (`PacingControllerConfig::padding_packet`) and
`SendSideController::Process` asked `RtpSender::MakePadding` to fill in empty padding when a placeholder left the
queue (`src/rtp/send_side_controller.cc`, padding branch; `src/rtp/rtp_sender.cc` `MakePadding`). libwebrtc asks the RTP
sender for the packets instead (`PacingController::ProcessPackets` -> `PacketRouter::GeneratePadding` ->
`RTPSender::GeneratePadding`, `compare/webrtc/reference/modules/rtp_rtcp/source/rtp_sender.cc:418-459`), which first
resends history packets on the RTX SSRC (`AppendPayloadPadding`, `rtp_sender.cc:461-490`;
`RtpPacketHistory::GetPayloadPaddingPacket`, `rtp_packet_history.cc:261-320`) and then builds empty padding of
`kMaxPaddingLength` 255 bytes (`BuildPaddingPacket`), and the pacer accounts the real size of every packet.
Our `loss::RtxSender` had no `GetPayloadPaddingPacket`.

## Fix

With `pacer v2` and `rtx v2` (everything else keeps the placeholder, so `pacer v1`, `rtx v1` and the defaults on
`main` are unchanged):

- `loss::RtxSender::GetPayloadPaddingPacket`: `PaddingMode::kRecentLargePacket` (`rtp_rtcp_impl2.cc:66`): the most
  recent large payload (replaced by a packet within `kOldPayloadPaddingSizeHysteresis` 100 bytes of it or 2^13 sequence
  numbers newer), RTX-encapsulated only if the sender accepts it.
- `rtp::RtpSender::GeneratePadding`: `AppendPayloadPadding` (while at least `kMinPayloadPaddingBytes` 50 are left, never
  over `kMaxPaddingSizeFactor` 3.0 x the target; video only, as `kRtxRedundantPayloads`), then empty padding on the RTX
  SSRC in `kMaxPaddingLength` 255-byte packets (audio: `PaddingBytesInPacket`, at least 50); with RTX and no
  transport-cc/abs-send-time it waits for a media packet.
- `cc::PacingController::SetPaddingGenerator`: the padding rate, the keep-alive (1 byte) and the first packet of a probe
  (1 byte) ask the generator; the packets are queued at the padding level with their real sizes; a generator that makes
  nothing still charges the padding budget (`pacing_controller.cc:496-508`).
- `SendSideController::SetPaddingSender` installs the generator and keeps the packets until the pacer releases them.
- Test bench: a forward packet on the RTX SSRC that carries a payload is counted as RTX, also when sent as padding, as
  `sm_compare` counts libwebrtc's (`compare/webrtc/webrtc_call.cc:443-446`).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_120_rtx_payload_padding.scn` (seeds 1-5) | `net.padding_kbps` 9.628 on seed 1 (expect < 2) | 0.18-0.48 |
| `testbench/scenarios/issue_120_rtx_payload_padding_capacity_drop.scn` (seeds 1-5) | `net.padding_kbps` 1.32 and 1.99 on seeds 4 and 5 (expect < 1) | 0.24-0.79 |
| `tests/unit/rtp/rtp_sender_test.cc` `RtpSender.GeneratePadding*`, `PayloadPaddingKeepsTheRecentLargePacket` | no `GeneratePadding` | passes |
| `tests/unit/cc/pacing_controller_test.cc` `PaddingGeneratorSizesAreAccounted`, `EmptyPaddingGeneratorChargesTheBudget`, `ProbeStartsWithGeneratedPadding` | no generator | passes |

## Results

Against libwebrtc (CI `compare-webrtc`, 10 seeds), this branch seeds 1-5:

| scenario | metric | main (v2 switches) | this PR | libwebrtc |
|---|---|---|---|---|
| issue_120_rtx_payload_padding (the `issue_23_handover` link) | net.padding_kbps | 9.6 (seed 1) | 0.18-0.48 | 0.6 |
| | net.rtx_kbps | 332 (seed 1) | 452-502 | 460 |
| | net.loss_pct | - | 4.9-6.2 | 6.0 |
| issue_120_rtx_payload_padding_capacity_drop (the `probe_v2_capacity_drop` link) | net.padding_kbps | 0.55-1.99 | 0.24-0.79 | 0.3 |
| | net.rtx_kbps | 29-41 | 29-41 | 29.6 |

The padding packet size is 255 B of padding plus the header (303 B on the wire) as libwebrtc, not 224 B.

The suite against the baseline: the two new scenarios, and the two scenarios that switch on both `pacer v2` and `rtx v2`
change (`issue_61_googcc_glue_congestive`, `issue_76_rtx_v2_keyframe_flush`). Seeds 1-5, main -> this PR (means):

| scenario | metric | main | this PR |
|---|---|---|---|
| issue_61_googcc_glue_congestive | net.padding_kbps (libwebrtc 0.6) | 2.0 | 0.24 |
| | net.rtx_kbps (libwebrtc 104) | 89 | 99 |
| | video.freeze_total_ms (libwebrtc 2148) | 3177 | 3993 |
| issue_76_rtx_v2_keyframe_flush | net.padding_kbps | 0.75 | 0.26 |
| | video.freeze_total_ms | 2621 | 2330 |

The freeze totals of the two scenarios move both ways per seed (a congestive-loss scenario where a resent packet
changes which frame is complete); the freeze gap to libwebrtc is not shown to come from padding and stays with #116.
Every `expect` line of the suite passes unchanged.
