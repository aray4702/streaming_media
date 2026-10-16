---
issue: 23
title: Receiver requests 11-78 keyframes per call where libwebrtc requests 0-1 (libwebrtc's receiver policy behind keyframe v2)
severity: sev:2
status: partial
area: area:loss, area:video
regression_tests: testbench/scenarios/issue_23_low_bandwidth.scn, testbench/scenarios/issue_23_handover.scn, testbench/scenarios/issue_23_outage_3s.scn, testbench/scenarios/issue_23_capacity_drop.scn, testbench/scenarios/issue_23_outage_500ms.scn, tests/unit/loss/keyframe_test.cc, tests/unit/loss/nack_rtx_test.cc, tests/unit/rtp/rtp_receiver_test.cc
---

# #23: Receiver requests 11-78 keyframes per call where libwebrtc requests 0-1

## Issue

The receiver asked for 5-78 keyframes per call where libwebrtc asks for 0-1, and each keyframe is about 4x a delta frame, so on a full link every request feeds the next loss. https://github.com/aray4702/streaming_media/issues/23 (Tester, re-scoped by the Owner to duplicate libwebrtc's receiver policy).

## Root cause

Three triggers that libwebrtc does not have (inventory 3.1 and 3.5, `docs/design/libwebrtc-parity.md`), in `src/loss/keyframe_request.cc` and `src/loss/nack_tracker.cc`:

1. `KeyframeRequester::Process`: a frame blocked longer than `max(200 ms, 2 x RTT)` asked for a PLI (the repair window), and a PLI was repeated after `2 x max(100 ms, 1.5 x RTT)` without a keyframe.
2. `NackTracker::GetNackBatch`: a gap given up after **10** retries asked for a keyframe (libwebrtc: 100 retries, `kMaxNackRetries`, and the entry is only erased, `nack_requester.cc`).
3. `NackTracker`'s list overflow tried to keep the gaps after the next keyframe (`RecoverKeyframeSpace`); libwebrtc clears the list and asks (`AddPacketsToNack`).

Attribution on `main`, scenario default seed (events): `low_bandwidth` 14 requests: 6 NACK give-ups + repair-window and retry PLIs + 1 FIR at stream start; `wifi_to_lte_handover` 18 (195 give-ups); `outage_3s` 5; `capacity_drop` 10. The stream-start FIR is a fourth trigger libwebrtc lacks (it asks only when the first assembled frame is not a keyframe, `RtpVideoStreamReceiver2::OnAssembledFrame`).

## Fix

Behind `keyframe v1|v2` (`KeyframeRequestConfig::version`, `NackConfig::version`, scenario keyword `keyframe`), default v1 = unchanged behaviour, as `program.md` 0.2 asks for a large port. v2 does what libwebrtc does:

- `NackTracker` v2: 100 retries, giving up requests nothing; a full list (1000) is cleared and requests a keyframe; the age limit 10000 as before.
- `KeyframeRequester` v2: no repair window, retry timer or receiver-side throttle (libwebrtc's `RtcpFeedbackBuffer` coalesces the triggers of one pass; so does `Process`); no request at stream start; a request when the first assembled frame is not a keyframe, on a decoder error, on a NACK list overflow (`OnExternalRequest`), and on the decode time-out.
- Events name the trigger: `keyframe/trigger_first_frame_delta`, `trigger_decoder_error`, `trigger_nack_list_full`, `trigger_decode_timeout`, `nack/list_full`.
- `issue_18_lntf_not_pli` is renamed `issue_18_random_loss_keyframes` (the Owner's request; same seed and `expect` lines, new description; baseline key renamed).

**Not from `reference/`.** The decode time-out (`VideoStreamBufferController`'s timeout tracker calling `VideoReceiveStream2::OnDecodableFrameTimeout`, which asks for a keyframe) has its call site in `reference/` but not its handler. Its values are libwebrtc's defaults as remembered: 200 ms before the first keyframe, 3 s afterwards, no request while a keyframe packet arrived within 200 ms, none when no packet came for 5 s. They are marked in `keyframe_request.h` and must be checked when the file is added (#57). Without a time-out a stream blocked by an unrepairable loss never recovers (the e2e `RecoversFromUnrepairableLoss` test showed 3% decodable).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `issue_23_low_bandwidth.scn` | does not parse on the base commit (no `keyframe v2`); with v1 14 requests | 1 (expect <= 2) |
| `issue_23_handover.scn` | as above; v1 18 | 1 |
| `issue_23_outage_3s.scn` | v1 5 | 1 (expect <= 3; libwebrtc 1) |
| `issue_23_capacity_drop.scn` | v1 10 | 1 |
| `issue_23_outage_500ms.scn` | v1 6-8 (Tester run 4) | 1 |
| `keyframe_test.cc` `KeyframeRequestV2.*` (5 tests) | new API | pass |
| `nack_rtx_test.cc` `NackTracker.V2*` (2 tests) | new behaviour | pass |
| `rtp_receiver_test.cc` `RtpReceiver.V2MissingFrameReferenceIsLeftToNack` | new behaviour | pass |

## Results

Mean of seeds 1-5, v1 -> v2 (libwebrtc from the `compare-webrtc` CI job on `main`, mean of 10 seeds):

| scenario | keyframe requests v1 -> v2 | libwebrtc | freeze_total_ms v1 -> v2 | libwebrtc | longest_freeze_ms v1 -> v2 | libwebrtc |
|---|---|---|---|---|---|---|
| low_bandwidth | 13.8 -> 1.0 | 0.7 | 668 -> 4403 | 12936 | 517 -> 3847 | 3279 |
| wifi_to_lte_handover | 17.6 -> 1.0 | 0 | 4130 -> 5448 | 4528 | 2100 -> 3524 | 1917 |
| outage_3s | 5.4 -> 1.0 | 1 | 3338 -> 3539 | 3222 | 3160 -> 3167 | 3222 |
| capacity_drop | 9.2 -> 1.0 | 0 | 2903 -> 5087 | 2538 | 1558 -> 3385 | 1875 |

The default (v1) is unchanged, so the suite against the baseline shows 0 regressions; the baseline only gains the five new scenarios and the renamed key.

## Remaining

The five `issue_23_*` scenarios now also lock the freeze level reached (worst of seeds 1-5, with margin), so the regression cannot grow; the PR is labelled `needs-human` because v2 loses to v1 on freezes until the default flips. The request count is at libwebrtc's, but v2 **raises freezes** on `capacity_drop`, `wifi_to_lte_handover` and `low_bandwidth`, above libwebrtc's on the first two, which the issue's acceptance forbids (so this is `Refs`, and v2 is not the default).

Mechanism (`issue_5_keyframe_flush`, seed 2, v2): the capacity drop at 10 s loses packets 2000-2150 in the bottleneck queue; every retransmission is lost too (RTX is sent into the same overloaded queue, 66 `rtx/resend` for 35 gaps), the frames stay blocked, and only the 3 s decode time-out ends the freeze (10.9 s -> 14.4 s). v1 gave up after 10 retries and asked for a keyframe after about 1 s. libwebrtc shows no such stall (0 keyframe requests, longest freeze 1.9 s), so the difference is upstream of this item: how soon the queue drains after the drop (congestion control, #19/#15/#61) and how the sender serves retransmissions (#76, `RtpPacketHistory`). Next step: re-measure after those land; flip the default (final PR) once freezes are within the parity band. Also left: the packet-buffer-cleared trigger (2048 packets, inventory 4.3), the large-sequence-discontinuity trigger, the missing-video-structure and H.264 parameter-set triggers, and the time-out values above (#57).
