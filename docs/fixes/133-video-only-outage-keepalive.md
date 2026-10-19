---
issue: 133
title: A video-only call never recovers from an outage of 3 s or more with pacer v2
severity: sev:1
status: partial
area: area:cc, area:video
regression_tests: testbench/scenarios/issue_133_pacer_v2_outage_3s.scn, testbench/scenarios/issue_133_pacer_v2_outage_8s.scn, testbench/scenarios/issue_133_pacer_v2_outage_15s.scn, testbench/scenarios/issue_133_all_v2_outage_3s.scn, testbench/scenarios/issue_133_all_v2_outage_8s.scn, testbench/scenarios/issue_133_all_v2_outage_15s.scn
---
<!-- sm-agent: fixer -->

# #133: A video-only call never recovers from an outage of 3 s or more with pacer v2

## Issue

In a video-only call (audio off) with `pacer v2` (alone or with all the v2 switches), a link outage of 3 s or more
is never recovered from: nothing is sent after the link is back and the user sees no video for the rest of the call
(`video.frames_rendered` 300 of 1100, one 30 s pause). Found by the Tester on f91fecb:
https://github.com/aray4702/streaming_media/issues/133.

## Root cause

The outage monitor (`src/cc/outage_monitor.cc`, `OutageMonitor::Process`) pauses the encoder when feedback has been
silent for `down_after`, and resumes it only from `OnFeedback`. `VideoSendController` then drops every captured frame
(`DropReason::kPaused`, `src/rtp/video_send_controller.cc:65`), so nothing reaches the pacer. With audio on, audio
packets keep the wire alive and the receiver's transport feedback ends the outage. Video-only, nothing is on the wire,
so no feedback can arrive, and the encoder is never resumed.

libwebrtc has the mechanism for this in `PacingController::ShouldSendKeepalive`
(`compare/webrtc/reference/modules/pacing/pacing_controller.cc:331`): while the pacer is paused or congested it sends
a 1-byte padding packet every `kCongestedPacketInterval` (500 ms) (`ProcessPackets`, l.417). Our port has the same code
(`src/cc/pacing_controller.cc:201`), but the pacer was never paused or congested here: the library paused the
encoder and left the pacer idle, so `sender.pacer.keepalives` stayed 0.

## Fix

`SendSideController::Process` (`src/rtp/send_side_controller.cc`) pauses the pacer together with the encoder when
the outage monitor pauses it, and resumes it with the encoder, when the pacer is `pacer v2`. The paused pacer sends
libwebrtc's keep-alive padding every 500 ms, the receiver's feedback tells the outage monitor the link is back, and
the existing recovery path (resume encoder, keyframe request, estimate reset) runs. `pacer v1` and `cc v1` are
unchanged, and `testbench/baseline.json` only gains the new scenarios.

## Regression tests

| Test | Fails before the fix | Passes after (seed 1) |
|---|---|---|
| `issue_133_pacer_v2_outage_3s.scn` | `frames_rendered` 300 (expect > 1000) | 1100, longest freeze 3282 ms |
| `issue_133_pacer_v2_outage_8s.scn` | `frames_rendered` 300 (expect > 850) | 950, pause 8275 ms |
| `issue_133_pacer_v2_outage_15s.scn` | `frames_rendered` 300 (expect > 650) | 740, pause 15275 ms |
| `issue_133_all_v2_outage_3s.scn` | same | 1102, longest freeze 3634 ms |
| `issue_133_all_v2_outage_8s.scn` | same | 952, pause 8244 ms |
| `issue_133_all_v2_outage_15s.scn` | same | 742, pause 15244 ms |

## Results

All six scenarios pass on seeds 1-5 (30 of 30 runs). Video resumes about 0.3 s after link-up on every outage length.

The scenario suite against the baseline (`tools/testbench_compare.py`): 0 regressions, 0 improvements; the six new
scenarios are added to the baseline.

## Remaining

- `cc v2` with `pacer v1` (the hybrid the Tester also lists) still never recovers: `pacer v1` has no keep-alive.
  It is not a shipped configuration, since the flip (#61) turns every v2 switch on and deletes v1.
- libwebrtc's own numbers for these video-only outage scenarios are not compared yet: they appear on
  `parity-data-pr/pr/<N>/` once this PR's CI has run, and the next run checks the recovery time against them.
- #134 (the same symptom on v1) is not touched.
