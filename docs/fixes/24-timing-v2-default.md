---
issue: 24
title: Video delay p95 1.5 s under 40 ms jitter with reordering while network queueing is under 100 ms; libwebrtc 142 ms
severity: sev:2
status: fixed
area: area:timing, area:video
regression_tests: testbench/scenarios/issue_24_timing_v2_default.scn, testbench/scenarios/issue_76_tcc_v2_high_jitter.scn, tests/unit/rtp/session_test.cc
---

# #24: Video delay p95 1.5 s under 40 ms jitter with reordering while network queueing is under 100 ms; libwebrtc 142 ms

## Issue

On the high-jitter link (40 ms jitter with reordering, no loss) video arrived
1.5 s late (p95) where libwebrtc's is about 150 ms, and on a capacity drop
(`issue_53_cwnd_pushback`) the receiver showed 16-21 fps where libwebrtc
shows 27-28. https://github.com/aray4702/streaming_media/issues/24, found by
the Tester (split from #19). The Owner's plan (15:07 UTC on the issue): port
libwebrtc's video receive timing behind `timing v1|v2` (#207, the first PR),
check the targets against libwebrtc, then make it the default. The Owner
(18:11 UTC on #186): "`timing v2` [...] join[s] when #24 [...] finish[es]".
This record is that last step.

## Root cause

The port itself is described in [24-timing-v2.md](24-timing-v2.md). What
remained was the default: `rtp::VideoStreamConfig::timing` and the test
bench both ran timing v1, the library's `VideoJitterEstimator`,
`VideoTiming` and `video::FrameScheduler`. Its 10 ms late-drop rule
(`FrameScheduler`, `src/video/frame_scheduler.cc`) throws away every frame
that misses its render time while a newer one is due. libwebrtc never does
that: `FrameDecodeTiming::OnFrameBufferUpdated`
(`compare/webrtc/reference/video/frame_decode_timing.cc:45`) skips a late
frame only when a later temporal unit is already decodable, which with every
delta frame referencing the previous one is only a keyframe. So with timing
v1 as the default, a call over a dropping link rendered a fraction of the
frames libwebrtc renders.

## Fix

`timing v2` is now the default, the way #61 made the other nine switches
the default (#186):

- `include/sm/rtp/session.h`: `VideoStreamConfig::timing` defaults to
  version 2 (libwebrtc's `VCMTiming`, `JitterEstimator`,
  `TimestampExtrapolator` and frame release). `timing::VideoTimingConfig`
  on its own keeps version 1, so its unit tests test what they name.
- `testbench/scenario.h`, `testbench/call.cc`: the test bench's `timing`
  defaults to v2 and is passed explicitly (`timing v1` still selects the
  library's own receive timing until v1 is deleted).
- `testbench/scenarios/issue_76_tcc_v2_high_jitter.scn`: the provisional
  `< 1300` from PR #180 goes back to the Owner's `< 300`, as the Owner's
  target list for this item asks.
- `tools/catalog.py` (4.6, 5.4) and `testbench/README.md`.

No algorithm changes: the receive timing is the one #207 ported and #208,
#211 and #214 measured against libwebrtc.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_24_timing_v2_default.scn` (the `issue_53_cwnd_pushback` link with the default timing) | `video.render_fps` 15.96 (expect >= 28), `video.freeze_total_ms` 4254 (expect < 5000) passes | 28.93, 3388 |
| `testbench/scenarios/issue_76_tcc_v2_high_jitter.scn` `video.e2e_delay_p95_ms < 300` (restored) | passes on seed 1 (180), fails on seed 9 (313) | 130-199 on seeds 1-10 |
| `tests/unit/rtp/session_test.cc` `Session.VideoStreamDefaultsToLibwebrtcsReceiveTiming` | `VideoStreamConfig{}.timing.version` is 1 | 2 |

## Results

Measured on top of the #61 flip (#186 head b7c4c53), where every other v2
switch is already the default, seeds 1-10. libwebrtc from `parity-data`
(main's comparison, mean of 10 seeds).

`issue_24_timing_v2_default`, `video.render_fps` / `video.freeze_total_ms`:

| seed | before (timing v1) | after (timing v2) |
|---|---|---|
| 1 | 16.0 / 4254 | 28.9 / 3388 |
| 2 | 18.0 / 3709 | 29.0 / 3067 |
| 3 | 20.5 / 2806 | 29.1 / 1616 |
| 4 | 18.3 / 5205 | 28.9 / 2835 |
| 5 | 17.3 / 5188 | 29.2 / 3793 |
| 6 | 20.8 / 4975 | 29.1 / 2339 |
| 7 | 17.2 / 4906 | 28.9 / 3393 |
| 8 | 20.0 / 4607 | 28.8 / 2455 |
| 9 | 16.9 / 5621 | 29.1 / 3398 |
| 10 | 17.0 / 5517 | 28.9 / 3393 |

libwebrtc on the same link: 27.1-27.6 fps, 1816-3617 ms of freezes.

The Owner's targets for this item (mean of seeds 1-10, range in brackets):

| scenario | metric | timing v1 | timing v2 | libwebrtc |
|---|---|---|---|---|
| `issue_53_cwnd_pushback` | `video.render_fps` | 18.2 (16-21) | 29.0 (29-29) | 27.4 (27-28) |
| `issue_76_tcc_v2_high_jitter` | `video.e2e_delay_p95_ms` | 202.2 (162-313) | 152.1 (130-199) | 150.2 (130-218) |
| `high_jitter` | `video.e2e_delay_p95_ms` | 202.2 (162-313) | 152.1 (130-199) | 150.2 (130-218) |
| `clean_link` | `video.e2e_delay_p95_ms` | 120.5 (88-169) | 123.5 (95-169) | 122.7 (103-165) |
| `issue_151_outage_3s_v2` | `video.freeze_total_ms` | 3413 (3142-4251) | 3340 (3201-4244) | 3241 (3183-3286) |

Parity report (`tools/parity_report.py`, ours from local seeds 1-10 against
libwebrtc from main's comparison): 317 gaps with timing v1, 240 with
timing v2, **88 closed and 11 opened**. The opened ones:

- `video.render_fps` ahead of libwebrtc by 1.5-2 fps on five low-rate links
  (`low_bandwidth`, `issue_23_low_bandwidth`, `issue_48_prober_low_bandwidth`,
  `issue_21_shallow_drop_repair`, `issue_154_shallow_drop_repair_v2`).
- `issue_61_googcc_glue_congestive` and `issue_12_lossbwe_congestive`
  (same link): `video.e2e_delay_p95_ms` 808 against libwebrtc's 420
  (timing v1 508), freezes 5.0 against 3.7. Timing v2 renders the frames
  timing v1 dropped as late (1194 rendered, libwebrtc 1195, timing v1 1036),
  and those are the frames that waited longest in our pacer: the pacer
  delay p95 of rendered frames goes from 355 to 609 ms, while e2e p50 moves
  to libwebrtc's (204 against 196; timing v1 270). The excess is send side,
  as #211 showed for `issue_24_timing_v2_render_fps`.
- `issue_127_pacer_v2_jitter_reorder_loss`: 3.3 freezes (753 ms) against
  libwebrtc's 1.1 (240 ms); timing v1 0.5 (121 ms). Delay matches
  libwebrtc (e2e p50 121 against 115, p95 187 against 185). Not yet
  attributed; see the PR.

The suite against the baseline is in the PR body.
