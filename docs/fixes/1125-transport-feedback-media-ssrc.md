---
issue: 1125
title: "interop: Chrome's bandwidth estimate toward our endpoint stays at 84 kbps (feedback to Chrome)"
severity: sev:2
status: fixed
area: area:cc
regression_tests: tests/interop/browser/CMakeLists.txt, tests/unit/rtp/rtp_receiver_test.cc, tests/unit/cc/transport_feedback_test.cc
---

# #1125: Chrome's bandwidth estimate toward our endpoint stays at 84 kbps

## Issue

In a call from headless Chrome to our endpoint (`sm_call_smoke`, and the
browser interop peer), Chrome's send-side estimate (`availableOutgoingBitrate`)
fell from its 300 kbps start to about 84 kbps and stayed there, even on
loopback, so its video arrived at about 13 fps. Chrome → Chrome on the same
machine reaches about 3.5 Mbps within 3 s. Found by the Builder in the
real-network calls of #1121 (PR #1123):
https://github.com/aray4702/streaming_media/issues/1125.

## Root cause

Our transport-cc feedback named media source SSRC 0. `rtp::Session` makes its
`cc::TransportFeedbackBuilder` with `media_ssrc = 0` (`src/rtp/session.cc:63`)
and nothing changed it, so every feedback message carried SSRC 0.
libwebrtc's `RTCPReceiver::HandleTransportFeedback`
(`modules/rtp_rtcp/source/rtcp_receiver.cc:897-901`) keeps a transport
feedback only when its media SSRC is the module's own or one it has
registered. Chrome therefore dropped all of our feedback. Its GoogCC got
no transport feedback, and its estimate fell from the start rate. libwebrtc's
generator sets `media_ssrc_ = packet.Ssrc()` on every packet it records
(`TransportSequenceNumberFeedbackGenenerator::OnReceivedPacket`,
`modules/remote_bitrate_estimator/transport_sequence_number_feedback_generator.cc:96`).
Our port did not.

Our own sender doesn't check the media SSRC, so calls between two of our
endpoints, the test bench and `sm_compare` (libwebrtc on both sides) never
showed it.

## Fix

`TransportFeedbackBuilder::OnPacket(seq, arrival, media_ssrc)` (a new
overload) records the packet's SSRC as the feedback's media SSRC, as
libwebrtc does. `RtpReceiver` passes each packet's SSRC to it. The SSRC is
the media or RTX SSRC of whichever packet came last, and Chrome registers both.
Nothing else in the feedback changes, so the test bench's metrics stay the
same.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `interop.Browser.chrome_offers` / `chrome_answers` (new `--bwe-kbps=1500 --bwe-seconds=10` check, `bweRampsUp`) | Chrome's estimate by second: 300, 260, 208, 166, 133, 106, 85, 84, 68 kbps | 2790 kbps (offers) and 3085 kbps (answers) from the first second sampled |
| `interop.BrowserSelf.*` (same check, Chrome → Chrome) | passes (3519 kbps) | passes |
| `tests/unit/rtp/rtp_receiver_test.cc` `RtpReceiver.TransportSequenceNumbersOfEveryPacketReachTheFeedbackBuilder` | `media_ssrc` 4369 (the builder's constructor value) vs 8738 | passes |
| `tests/unit/cc/transport_feedback_test.cc` `TransportFeedbackV2.MediaSsrcIsTheLastReceivedPacketsSsrc` | does not build (no overload) | passes |

There is no test bench scenario. The test bench runs our stack on both sides,
and our sender takes feedback whatever its media SSRC, so no scenario can see
this. The browser interop test is the end-to-end regression test.

## Results

Chrome's `availableOutgoingBitrate` on loopback (macOS, Chrome headless,
640x480 fake camera):

| call | before | after |
|---|---|---|
| Chrome → Chrome (self-test) | 3519 kbps | 3519 kbps |
| Chrome offers → our endpoint | 300 → 68 kbps over 9 s | 2790 kbps |
| Chrome answers → our endpoint | not measured (the issue's `sm_call_smoke` call: about 84 kbps) | 3085 kbps |

The scenario suite against the baseline (`tools/testbench_compare.py`):
0 regressions, 0 improvements. Only the media SSRC field of the feedback changed.
