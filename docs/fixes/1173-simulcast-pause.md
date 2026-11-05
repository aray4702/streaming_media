---
issue: 1173
title: simulcast: layers the SFU pauses are still encoded and sent
severity: sev:2
status: fixed
area: area:video, area:pc
regression_tests: testbench/scenarios/issue_1173_simulcast_pause.scn, tests/unit/rtp/simulcast_send_controller_test.cc, tests/unit/pc_endpoint/renegotiation_test.cc
---

# #1173: simulcast: layers the SFU pauses are still encoded and sent

## Issue

An SFU such as LiveKit pauses the simulcast layers no subscriber watches: its answer or re-offer carries
`a=simulcast:recv q;h;~f` (a paused RID) or leaves a layer out. `pc::PeerEndpoint` applied that state to its
`video_simulcast()` encodings (C17, C18), but nothing reached the encoder: `rtp::SimulcastSendController` kept
encoding the paused layer and the session kept sending it, so the uplink carried a layer nobody receives (2.5 Mbps
for a paused 720p top layer). libwebrtc M157 stops encoding it and gives its rate to the other layers. Found by the
Builder's window while merging C17 with C18 (#1170): https://github.com/aray4702/streaming_media/issues/1173.

## Root cause

The layer state stopped at the endpoint. `PeerEndpoint::UpdateSimulcastLayers` (`src/pc/peer_endpoint.cc`, main
a1266a4) set `SimulcastEncoding::active` and nothing else; the session had no per-stream state, and
`SimulcastSendController` built its encoder config once (`cfg_.encoder.simulcast_layers[i].active` always true),
so `video::CreateEncoderStreams`, `SetupCodec`, `SimulcastRateAllocator` and `SimulcastAllocationLimitsFor`, which all
honour `active`, never saw an inactive layer. In libwebrtc, `SdpOfferAnswerHandler`'s
`UpdateSimulcastLayerStatusInSender` sets `RtpEncodingParameters::active`, and
`WebRtcVideoSendStream::SetRtpParameters` (`media/engine/webrtc_video_engine.cc`) reconfigures the encoder on any
change of a layer's active state (`CreateVideoEncoderConfig` copies it into `simulcast_layers[i].active`;
`VideoStreamEncoder::ConfigureEncoder` runs `ReconfigureEncoder` at once when the frame size is known).

## Fix

- `rtp::Session::SetVideoSendActive` / `video_send_active` (new, additive): a video send stream's
  `RtpEncodingParameters::active`. `PeerEndpoint::UpdateSimulcastLayers` sets it for every layer after each remote
  description. The session does nothing else with it: libwebrtc's RTP modules stay active
  (`RtpVideoSender::SetActiveModulesLocked`; only the video-layers-allocation extension, not negotiated by default,
  deactivates a module by allocation).
- `rtp::SimulcastSendController` reads it at each captured frame and each `MaybeEncodePendingFrame`, and
  `SetLayerActive(layer, active, now)` (new) sets and applies it at once. A change runs `ReconfigureEncoder`: new
  streams, codec, rate allocator and bitrate adjuster, the frame dropper reset, the new allocation limits (only the
  active streams: 720p with the top layer paused is max 900 kbps, pad-up 330 kbps) with the reallocation
  (`OnEncoderConfigurationChanged`), and the new allocation to the layer encoders (`SetEncoderRates` with the last
  rate settings). `SimulcastRequiresEncoderReset` ports `RequiresEncoderReset`: a stream turned off needs no new
  encoder (the others keep their delta frames), a stream turned back on does, and every layer's next frame is then a
  keyframe. The size-change path keeps re-initializing as before.
- The test bench's `simulcast_pause <i> at <t>` / `simulcast_resume <i> at <t>` drive it, and `sm_compare` does the
  same on libwebrtc's side with `VideoSendStream::ReconfigureVideoEncoder` and the layer's `active` (what
  `SetRtpParameters` ends in).

Not ported (remaining differences, in `docs/design/libwebrtc-parity.md` 10.24): with every encoding inactive
libwebrtc stops the send stream (`VideoSendStreamImpl`), where ours encodes nothing but keeps its last allocation
limits; and libwebrtc allows quality scaling when one layer is active (`is_quality_scaling_allowed`), ours keeps it
off for simulcast.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_1173_simulcast_pause.scn` | no `simulcast_pause` directive (the top layer would run 30 fps, `layer2_fps` 30 vs expect < 22) | `layer2_fps` 19.87, `layer2_keyframes` 2, `layer1_kbps` 557 |
| `tests/unit/rtp/simulcast_send_controller_test.cc` `SimulcastSend.InactiveLayerIsNotEncodedAndItsRateGoesToTheActiveOnes` | no `SetLayerActive` (the layer is encoded) | no frame or packet on the paused SSRC; middle layer 500 -> 700 kbps; limit 900 kbps |
| `SimulcastSend.ReactivatedLayerRestartsWithAKeyframe` | as above | first frame after resume is a keyframe, limit back to 3400 kbps |
| `SimulcastSend.SessionActiveStateIsTakenAtTheNextFrame` | no session state | the session's state is applied at the next frame |
| `SimulcastSend.RequiresEncoderResetOnlyWhenAStreamComesBack` | no function | passes |
| `tests/unit/pc_endpoint/renegotiation_test.cc` `PcRenegotiation.SfuReofferPausesAndResumesASimulcastLayer` | with the endpoint's state not passed on: the paused layer keeps encoding (137 vs 76 frames) and reaching the SFU (824 vs 502 packets) | nothing more on its SSRC while paused; resumes with a keyframe |

## Results

`issue_1173_simulcast_pause` (macOS arm64, Release), seeds 1-5:

| seed | layer2_fps | layer2_keyframes | layer1_kbps | layer2_kbps | freeze_count |
|---|---|---|---|---|---|
| 1 | 19.87 | 2 | 557.0 | 1636 | 0 |
| 2 | 19.87 | 2 | 555.8 | 1647 | 0 |
| 3 | 19.87 | 2 | 553.1 | 1630 | 0 |
| 4 | 19.87 | 2 | 560.6 | 1631 | 0 |
| 5 | 19.87 | 2 | 558.5 | 1627 | 0 |

Without the pause the top layer runs 30 fps at about 2450 kbps (`simulcast_clean_link`) and the middle layer stays
at its 500 kbps target. The scenario suite against the baseline is unchanged (no other scenario pauses a layer by
signaling; the default path changes only when an encoding's active state changes). libwebrtc's values for the new
scenario come from this PR's `compare-webrtc` run.
