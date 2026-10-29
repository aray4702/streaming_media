---
issue: 484
title: RTCP: the audio report block rides on every video RTCP packet; libwebrtc sends it only in the audio stream's own RTCP (5 s), so Opus FEC sees other loss
severity: sev:3
status: fixed
area: area:loss, area:audio
regression_tests: testbench/scenarios/issue_484_audio_report_blocks.scn, tests/unit/rtp/session_test.cc
---

# #484: the audio report block rides on every video RTCP packet

## Issue

Our receiver put the audio SSRC's report block in every RTCP compound it sent: the 1 s video receiver reports and
every NACK and PLI. libwebrtc sends it only in the audio receive channel's own RTCP, about every 5 s. Our Opus encoder
therefore got 5 to 70 times as many loss reports, over short intervals, and its in-band FEC (loss percentage) moved
where libwebrtc's did not. Found by the harness audit, #478 row 2, on `main` 67df95b
(https://github.com/aray4702/streaming_media/issues/484).

## Root cause

`Session::AddAudioReceive` registered the audio receiver's `ReceiveStatistics` with the session's one `RtcpEngine`
(`src/rtp/session.cc:174` before this change), the same engine the video receive stream reports through, so every
report and every feedback compound carried the audio block.

libwebrtc gives each receive stream its own statistics and its own RTCP module:
- `audio/channel_receive.cc:608`: `rtp_receive_statistics_(ReceiveStatistics::Create(...))`, and
  `CreateRtpRtcpModule` (`:115-139`) builds a receiver-only `RtpRtcp` (`.audio = true`, `.receiver_only = true`,
  `RtcpMode::kCompound`) on it. Its `RTCPSender` reports at `kDefaultAudioReportInterval` 5 s
  (`modules/rtp_rtcp/source/rtcp_receiver.cc:85`), the first at half the interval (`rtcp_sender.cc:171`).
- The audio receive module has no CNAME (nothing calls `SetCNAME` on it; only `channel_send.cc:804` does), so its
  RR goes out without SDES (`rtcp_sender.cc:713`), and `enable_non_sender_rtt` is off by default, so no RRTR.
- `video/video_receive_stream2.cc:255`: the video stream's own statistics, so its RRs, NACKs and PLIs carry only the
  video and RTX blocks.

## Fix

`SessionConfig::audio_receive_rtcp` (default off in this PR, as the coordinator's plan on the issue asks when the suite
moves). On, `AddAudioReceive` gives the audio receive streams their own `RtcpEngine`: the session's RTCP config with
no CNAME, `has_video = false` (the 5 s audio interval, first report at 2.5 s, no random draw), no RRTR, the session's
seed (libwebrtc seeds every `RTCPSender` with the same construction time, #517). `Process` sends its reports and
`NextProcessTime` includes its timer. The video compounds then carry only the video and RTX blocks.

Readers of the audio block on the sender: the Opus encoder's loss input (`kReportBlock` events,
`ChannelSend::OnReportBlockDataUpdated` in the bench), the RTT (`RtcpIncoming::rtts`, every block about a local media
SSRC) and the CallStats periodic update (`last_report_block_`). The RTT still gets a sample from the video blocks
every second, so only the audio stream's own samples become rarer, as in libwebrtc (and as #568's per-stream RTT,
PR #587, needs).

Test bench: `audio_receive_rtcp on|off` (our side; libwebrtc's side always reports per stream), and the metric
`rtcp.audio_report_blocks`, counted on both stacks by the shared `TraceAudioRtcp` rule on the RTCP that reaches the
sender. It is not in `sm_compare`'s compared list yet, so it opens no parity cell while the switch is off.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_484_audio_report_blocks.scn` | base `sm_testbench` rejects `audio_receive_rtcp` (exit 2); without the switch `rtcp.audio_report_blocks` 408 (expect <= 8) | 6 (seed 1) |
| `tests/unit/rtp/session_test.cc` `Session.AudioReceiveStreamReportsInItsOwnRtcpEveryFiveSeconds` | the audio block is in every compound with the video blocks | an RR with the audio block alone (32 bytes, no SDES), first at 2500 ms, 2.5-7.5 s apart |
| `tests/unit/rtp/session_test.cc` `Session.AudioReceiveRtcpEngineExistsOnlyWithAnAudioReceiveStream` | no such engine | created with the first audio receive stream, only with the switch |

## Results

Audio report blocks the sender receives on seed 1, against libwebrtc's (`parity-data` `traces/<scenario>.1.webrtc.audiosend.csv`), and the first audio packet whose payload differs from libwebrtc's:

| scenario | blocks off | blocks on | libwebrtc | block times on (ms) | libwebrtc's (ms) | first differing packet off -> on |
|---|---|---|---|---|---|---|
| `high_jitter` | 408 | 6 | 6 | 2530, 6047, 8682, 15062, 21711, 27374 | the same | 2.05 s -> 4.21 s (1366/1500 the same) |
| `random_loss_5pct` | 298 | 6 | 6 | 2540, 6057, 8692, 15072, 21721, 27384 | the same | 1.61 s -> 2.55 s |
| `clean_link` | 32 | 6 | 6 | 2525, 6042, 8677, 15057, 21706, 27369 | the same | 4.21 s -> 4.21 s |

On `random_loss_5pct` the first block's fraction lost is 6/256 against libwebrtc's 10/256 (the link drops different
packets once the media differs), and the Opus packets differ from the next one on.

`issue_484_audio_report_blocks` on seeds 1-5: `rtcp.audio_report_blocks` 6, 7, 7, 6, 6; all expectations pass.

The scenario suite against the baseline: no metric changes with the switch off; the only addition is the new metric
`rtcp.audio_report_blocks` in every scenario's results and the new scenario.

## Part 2: the default flip

`SessionConfig::audio_receive_rtcp` and the test bench's `audio_receive_rtcp` now default to on, libwebrtc's
behaviour (the flip decided on the issue), and `rtcp.audio_report_blocks` is in `sm_compare`'s compared metrics
(tie floor 1). `issue_484_audio_report_blocks` no longer sets the switch, so it runs the default: the base commit's
`sm_testbench` gives 408 blocks (expect <= 8), this one 6-7 on seeds 1-5.

Parity, seeds 1-10 of all 138 scenarios, our side run locally (switch off = `main`, switch on = this change)
against libwebrtc's per-seed values on `parity-data` (`compare-webrtc.json` for e212cc5; libwebrtc's side of
`webrtc_call.cc` is unchanged since, apart from counting the new metric): **70 gaps -> 57, 34 closed and 21
opened**, none changed direction. The 5 opened A/V-offset cells without an owner are placed in
`tools/parity_owners.json` (#474 for the three congestive copies, behind; #411 for `issue_12_lossbwe_policer` and
`issue_150_gcc_v2_high_jitter_seed10`, ahead).

**Guard rail:** `issue_224_congestive_e2e_flip` fails on seed 1 with the flip: `video.e2e_delay_p95_ms` 773.8
(expect < 655) and `sync.av_offset_p95_ms` 498.3 (expect < 440). Seeds 1-10:

| metric | off (`main`) | on | libwebrtc |
|---|---|---|---|
| e2e p95 (ms) | 396 425 302 336 356 323 377 350 302 318, mean 349 | 774 536 330 369 257 474 342 309 365 425, mean 418 | 383 374 388 317 331 365 425 332 244 419, mean 358 |
| A/V offset p95 (ms) | 195 210 146 167 222 197 192 232 145 185, mean 189 | 498 303 117 172 141 277 175 202 170 229, mean 228 | 175 144 239 130 190 187 235 205 109 192, mean 180 |

On seed 1 the sender's audio report blocks now match libwebrtc's in number and time, and the call parts from
libwebrtc's at 3 s, before either change matters: libwebrtc's delay-based estimate detects overuse (2116 kbps)
where ours stays at 3022 kbps, with the switch on or off (`parity-data` `traces/issue_224_congestive_e2e_flip.1.webrtc.bwe.csv`).
The RR loss fraction GoogCC gets also differs on both settings (ours 24-170/256 at the loss-based updates,
libwebrtc's 0-3). The line encodes behaviour libwebrtc has (its worst seed is 425 ms), so this change does not
move it; the PR asks for the decision. `per_stream_rtt on` with the flip gives seed 1 the same 774 ms (mean 394).
