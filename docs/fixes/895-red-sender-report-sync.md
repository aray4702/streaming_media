---
issue: 895
title: issue_12_lossbwe_shallow audio jitter-buffer delay and mouth-to-ear ahead of libwebrtc
severity: sev:3
status: fixed
area: area:sync, area:audio
regression_tests: testbench/scenarios/issue_895_red_sender_report_sync.scn, tests/unit/rtp/rtcp_engine_test.cc, tests/unit/rtp/rtp_sender_test.cc
---

# #895: issue_12_lossbwe_shallow audio jitter-buffer delay and mouth-to-ear ahead of libwebrtc

## Issue

On `issue_12_lossbwe_shallow`, our `audio.jb_delay_p95_ms` and `audio.mouth_to_ear_p95_ms` were about 12-16 ms
lower than libwebrtc's on 10 seeds (main 48de6b4: 79 vs 96 and 134.1 vs 151.0). That is a parity gap in the
"ahead" direction (https://github.com/aray4702/streaming_media/issues/895). The window filed it on the Owner's
decision of 2026-10-30 ("chase it"). #910 and #928 removed earlier forward-link differences.

## Root cause

On seed 1 with #931 (#893) applied, the forward link is identical to libwebrtc's for the whole call, yet the audio
trace splits at 12.00 s: NetEq's target is 33 vs 34 ms, then libwebrtc's goes 67 → 82 at 28.00 s and → 97 at
34.00 s, while ours stays at 67. Those are whole-second `RtpStreamsSynchronizer::UpdateDelay` runs raising audio's
minimum playout delay. A yardstick, with sync rows from both stacks' `Syncable::GetInfo` at each run, showed every
sync input equal (audio and video current delay, receive times, RTP timestamps) **except the relative delay**.
libwebrtc's was 5-9 ms larger. Ours kept the filtered diff at 26-27 ms, under `kMinDeltaMs` (30), so the steps
never happened.

The relative delay comes from each stream's RTP-to-NTP mapping, which the sender reports feed. The video SRs were
identical. The audio SRs carried different RTP timestamps for the same NTP time:

- libwebrtc's `RTCPSender::BuildSR` (`rtcp_sender.cc:350-366`) extrapolates the last frame's RTP timestamp by
  `((now_us + 500) / 1000 - last_frame_capture_time.ms()) * rtp_clock_rates_khz_[last_payload_type_]`: whole
  milliseconds, at the rate registered for the payload type of the last frame (`OnSendingRtpFrame`).
- `ChannelSend::SetEncoder` registers only the codec's payload type (`channel_send.cc:645`). With RED, the default,
  every audio frame is sent with RED's payload type, which has no rate, so the report falls back to
  `kBogusRtpRateForAudioRtcp`, 8 kHz (`rtcp_sender.cc:355-360`, `rtp_rtcp_defines.h:51`).
- Ours (`src/rtp/rtcp_engine.cc:128-129`, `RtpSender::SenderState`) extrapolated at the codec's 48 kHz in
  microseconds.

The time between the last audio frame and the report is 0-20 ms, so libwebrtc's audio SR RTP timestamp is
40 × that many samples lower than ours. On seed 1 the shortfalls are 400, 280, 80, 440, 560, 680 and 720 samples,
which are 40 × 10, 7, 2, 11, 14, 17 and 18 ms. That shifts the audio mapping and the relative delay.

## Fix

- `RtcpEngine::BuildReport` extrapolates the SR's RTP timestamp as `BuildSR` does: the report time rounded to the
  ms, less the last frame's time in ms, times the rate in kHz.
- `RtpSender::SenderState` reports the rate registered for the last frame's payload type: 8 kHz
  (`kBogusRtpRateForAudioRtcp`) for audio sent as RED, the codec's rate otherwise. Video keeps 90 kHz, which is
  both what `RtpVideoSender` registers and the fallback.
- Yardstick, kept because it is generally useful: a `sync` row in the bwe trace at each whole second on both
  stacks (`BweTraceRow::Kind::kSync`). It has both streams' `Syncable::GetInfo` and the relative delay, ours from
  the synchronizer and libwebrtc's from its own `RtpToNtpEstimator` and `StreamSynchronization::ComputeRelativeDelay`
  on the same values. `issue_12_lossbwe_shallow` joins `sm_compare`'s default traced scenarios.

With this and #931, seed 1's forward link and the whole audio trace are identical to libwebrtc's. Without #931 the
audio trace is identical through 34.96 s. After that, #931's padding difference at 25.028 s reaches the video
timing.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_895_red_sender_report_sync.scn` | `audio.jb_delay_p95_ms` 70 (expect >= 90), `audio.mouth_to_ear_p95_ms` 125 (expect >= 144) | 100, 155 (libwebrtc seed 1: 100, 155.2); passes on seeds 1-5 |
| `tests/unit/rtp/rtcp_engine_test.cc` `RtcpEngine.SenderReportExtrapolatesInWholeMilliseconds` | `rtp_timestamp` 4921 (expected 4928) | passes |
| `tests/unit/rtp/rtp_sender_test.cc` `RtpSender.SenderReportRateIsTheLastPayloadTypesRegisteredRate`, `RtpSender.AudioRedCarriesThePreviousFrame` | RED audio reports 48000 | 8000 |

## Results

`issue_12_lossbwe_shallow`, seeds 1-10 (test bench; libwebrtc's per-seed values from `parity-data`):

| metric | main 48de6b4 | this fix | libwebrtc |
|---|---|---|---|
| `audio.jb_delay_p95_ms` | 70 80 80 80 70 80 80 70 90 90 (mean 79.0) | 100 100 100 90 90 90 90 90 90 100 (94.0) | 100 100 80 100 100 90 100 90 110 90 (96.0) |
| `audio.mouth_to_ear_p95_ms` | mean 134.1 | 149.1 | 151.0 |
| `sync.av_offset_p95_ms` | mean 113.3 | 107.1 | 114.9 |
| `video.freeze_total_ms`, `net.loss_pct`, `cc.utilization_pct` | 1018.6, 6.4, 55.7 | unchanged on every seed | 1401.8, 6.2, 55.4 |

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_127_pacer_v2_jitter_reorder_loss | audio.late_discards | 19 | 16 | improved |
| issue_12_lossbwe_shallow | audio.jb_delay_p95_ms | 70 | 100 | REGRESSED |
| issue_12_lossbwe_shallow | audio.mouth_to_ear_p95_ms | 125 | 155 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed10 | audio.late_discards | 114 | 108 | improved |
| issue_415_cross_80pct_standing_queue | audio.jb_delay_p50_ms | 125 | 150 | REGRESSED |
| issue_453_h264_missing_packets_high_jitter | sync.av_offset_p95_ms | 40.2 | 63.13 | REGRESSED |
| issue_737_outage_15s_100ms | audio.jb_delay_p95_ms | 180 | 210 | REGRESSED |
| issue_737_outage_15s_100ms | audio.mouth_to_ear_p95_ms | 344.3 | 374.3 | REGRESSED |
| issue_895_red_sender_report_sync | (new scenario) | - | - | new |
| lossy_feedback | video.longest_freeze_ms | 1 | 25 | REGRESSED |

7 regression(s), 2 improvement(s)

Why each worse metric is acceptable. All of them come from the A/V sync moving toward libwebrtc's, because the
audio SR mapping now matches (libwebrtc's value on the same seed from `parity-data`):

- `issue_12_lossbwe_shallow` jb_delay_p95 70 → 100 and mouth_to_ear_p95 125 → 155: libwebrtc seed 1 is 100 and
  155.2. This is the item's target.
- `issue_415_cross_80pct_standing_queue` jb_delay_p50 125 → 150: libwebrtc's mean is 151.3 (ours on main 136.5).
- `issue_453_h264_missing_packets_high_jitter` av_offset_p95 40.2 → 63.1: libwebrtc's seed is 61.9 (mean 60.4).
- `issue_737_outage_15s_100ms` jb_delay_p95 180 → 210 and mouth_to_ear_p95 344.3 → 374.3: libwebrtc seed 1 is 200
  and 364.2. The distance halves (20 → 10 ms), now on the other side.
- `lossy_feedback` longest_freeze 1 → 25 ms: no freezes on either build (freeze_total 0 on all 10 seeds). This is
  the longest sub-threshold render gap. Seeds 1-10 on main are 1 14 19 1 24 34.1 23 5.5 31.5 33.5, with the fix
  25 5 26 4.4 1 34.1 30.3 5.5 31.5 33.5, and libwebrtc's are 2 5 25 5.1 1 0.6 25 5.7 31.5 33.5. The fix matches
  libwebrtc on 7 seeds (main: 4). Seed 1 moves away.
