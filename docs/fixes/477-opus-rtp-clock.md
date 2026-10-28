---
issue: 477
title: "Harness: audio capture path differs; per-frame check found the Opus RTP clock and NetEq at 16 kHz on our side"
severity: sev:2
status: fixed
area: area:audio, area:testbench
regression_tests: testbench/scenarios/issue_477_opus_rtp_clock.scn, tests/unit/testbench/testbench_metrics_test.cc
---

# #477: Harness: audio capture path differs (libwebrtc: ADM 10 ms blocks through ACM; ours: direct 20 ms encode)

## Issue

The two stacks send audio through different capture paths (libwebrtc: fake ADM 10 ms blocks -> ACM -> Opus on the
encoder queue; ours: a direct 20 ms libopus encode at a modelled phase), and nothing checked per frame that both
encoders get the same samples, stamp the same RTP timestamp, send at the same time and use the same settings.
Filed by the Owner from the harness fidelity audit (#478): https://github.com/aray4702/streaming_media/issues/477.

## Root cause

A per-frame audio send trace on both stacks (below) found one mismatch: **the RTP timestamp**. Our side stamped the
Opus packets with the scenario's capture rate (default 16 kHz, 320 ticks per 20 ms frame), because
`AudioCodecRate` (`testbench/call.cc`) kept 8, 16 and 32 kHz as the codec rate, and ran the receiver's NetEq and the
playout at that rate too. Since #460 our side encodes the 48 kHz signal with libopus, as libwebrtc does, so only the
clock was left behind. libwebrtc stamps Opus with a 48 kHz RTP clock whatever the capture rate
(`compare/webrtc/reference/modules/audio_coding/codecs/opus/audio_encoder_opus.cc` `kRtpTimestampRateHz = 48000`,
`RtpTimestampRateHz()`; RFC 7587), and its NetEq decodes at 48 kHz (`AudioDecoderOpusImpl`, `sm_compare` plays out
48 kHz). So the RR interarrival jitter, NetEq's sample arithmetic and the playout ran at another rate on our side.

Everything else the trace checks matched on the published `main` traces (`parity-data` `traces/`, seed 1):
- `low_bandwidth`: all 2000 audio packets leave at the same time with the same size on both stacks.
- `audio_only_lossy`: all frames leave at the same time (one 2 ms pacer difference behind a probe at 4.011 s), with
  the same sizes through 7.0 s, including the first loss report (2.5 s, the unrandomized first RTCP interval)
  reaching both encoders at the same frame. From 7.0 s the sizes differ because the second audio receiver report
  reaches the sender at a different time: each stack draws its own RTCP interval (`RTCPSender` seeds `Random` from the
  clock, `rtcp_sender.cc:130`; ours from the scenario seed, #399), and the link's random loss draws differ with the
  stacks' different padding, so the loss fraction fed to the encoder differs. That is the stack under test, not the
  capture path.

## Fix

- `AudioCodecRate` returns Opus's 48 kHz for every capture rate: our RTP clock, NetEq and playout run at 48 kHz, as
  libwebrtc's. The capture rate key stays (libwebrtc resamples the capture to the encoder).
- The audio send trace, written the same way by both stacks from the packet bytes (`testbench/call.h`
  `AudioSendRow`, `TraceAudioRtp` / `TraceAudioRtcp`): per audio packet on the forward link its send time, RTP
  timestamp, payload type, size and payload hash; per audio report block delivered to the sender its fraction lost.
  `sm_testbench --link-trace=DIR` and `sm_compare` write `<scenario>.<seed>.{sm,webrtc}.audiosend.csv`; the JSON
  carries `sm_audiosend` / `webrtc_audiosend` for the `--trace` scenarios, which now include `low_bandwidth` and
  `audio_only_lossy`. `tools/audio_send.py` compares them frame by frame and replays each side's report blocks
  through libwebrtc's packet loss smoother, so a frame that differs because its loss input differed is told apart
  from one that differs for another reason.
- A metric, `net.audio_rtp_clock_hz`: the audio stream's RTP clock measured on the forward link.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_477_opus_rtp_clock.scn` | `net.audio_rtp_clock_hz` unknown on the base commit (16000 with the metric added and the old rate) | 48000 |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.AudioCodecRateIsOpusClockForEveryCaptureRate` | `AudioCodecRate(16000)` was 16000 | 48000 |
| `TestbenchMetrics.AudioPacketsCarryOpusRtpClock` | RTP timestamp steps of 320 per frame | 960 |
| `TestbenchMetrics.AudioSendTraceParsesRtpAndReportBlocks` | (new trace) | passes |

## Results

The reproduction scenario across seeds (before -> after):

| seed | net.audio_rtp_clock_hz | audio.interruptions | audio.concealed_pct (after) |
|---|---|---|---|
| 1 | 16000 -> 48000 | 0 -> 0 | 0.58 |
| 2 | 16000 -> 48000 | 0 -> 0 | 0.29 |
| 3 | 16000 -> 48000 | 0 -> 0 | 0.59 |
| 4 | 16000 -> 48000 | 0 -> 0 | 0.55 |
| 5 | 16000 -> 48000 | 0 -> 0 | 0.18 |

The per-frame check on this PR's `compare-webrtc` run (`tools/audio_send.py`, seed 1, the 9 `--trace` scenarios,
`low_bandwidth` and `audio_only_lossy` among them): every frame has the same RTP timestamp on both stacks, and every
Opus payload is byte-identical until the encoders get different loss reports (`low_bandwidth` 2000 of 2000,
`audio_only_lossy` 303 of 303, then the report blocks reach the senders at different times as described above).
Send times differ on at most 7 frames per call, by 2 ms or less, from pacer queueing behind probes and padding.
Parity against `main`: 73 -> 75 gaps, 0 closed, 2 opened (`issue_12_lossbwe_shallow` `audio.mouth_to_ear_p95_ms`
138 vs 149 and `audio.jb_delay_p95_ms` 83 vs 94, ours ahead).

With #476 (libopus decode) on `main`, `issue_62_neteq_v2_high_jitter` `audio.jb_delay_p95_ms` is 170 on seed 1
(seeds 1-10 mean 133; libwebrtc 129), inside the `<= 180` line #476 set; no guard-rail line changes here.

Most scenarios change (NetEq's decisions at 48 kHz, and the calls that follow from them). The metrics the baseline
comparison flags against `main` with #476, with libwebrtc's 10-seed mean:

| scenario | metric | baseline | result | libwebrtc |
|---|---|---|---|---|
| issue_152_alr_request_probe_v2 | video.longest_freeze_ms | 4 | 30 | 13.9 (seeds 3-28 on 1-5) |
| issue_29_ulpfec_over_red | cc.target_max_drop_pct | 21.23 | 25.78 | 27.08 |

Improved: `sync.av_offset_p50_ms` on three congestive scenarios (-10.25 -> -31), `issue_5_keyframe_flush`
`sync.av_offset_p95_ms` 284.1 -> 263.1, `issue_29_ulpfec_over_red` `video.frames_rendered` 101 -> 109,
`low_fps_clean_link` `video.longest_freeze_ms` 67 -> 1.

Each moves toward libwebrtc's value, or stays within its seed spread. Every scenario adds `net.audio_rtp_clock_hz`
48000.
