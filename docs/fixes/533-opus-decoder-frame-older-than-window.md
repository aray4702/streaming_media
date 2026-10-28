---
issue: 533
title: Test bench Opus decoder reads freed memory when a frame arrives over 5 s late
severity: sev:1
status: fixed
area: area:audio, area:testbench
regression_tests: testbench/scenarios/issue_533_opus_decoder_frame_older_than_window.scn, tests/unit/testbench/opus_decoder_test.cc
---

# #533: Test bench Opus decoder reads freed memory when a frame arrives over 5 s late

## Issue

On a link whose jitter has a 2 s standard deviation, some audio packets
arrive more than 5 s after later ones. The test bench's Opus decoder then
read a freed heap block: the ASan+UBSan build aborted with
`heap-use-after-free` and the native build read garbage silently. Found by
the Tester on efafe01:
https://github.com/aray4702/streaming_media/issues/533. The bug is in the
bench (`testbench/`), not the library. No scenario in the suite had jitter
above 200 ms, so no suite metric was affected.

## Root cause

`OpusAudioDecoder::Insert` (`testbench/opus_decoder.h:184-192` on efafe01,
added by #487 for #476) keeps the last `kRemembered` = 250 frames (5 s) in a
`std::map` ordered by timestamp. It stored the new frame with
`insert_or_assign`, trimmed the map back to 250 by erasing `begin()`, and
returned `it->second.samples`. A frame older than all 250 remembered ones is
inserted at `begin()`, so the trim erased the frame just inserted and `it`
dangled. The insert also decoded all 250 later frames again from that frame's
state, and moved `state_before_first_` to it.

## Fix

`Insert` now checks, after decoding, whether the frame is older than every
remembered frame while the window is full (`frames_.size() >= kRemembered`
and `ts < frames_.begin()->first`), that is, whether the trim would erase
it. Such a frame's decoded samples are returned, but it is not stored, the
later frames are not decoded again, and the decoder goes back to the state
after the last frame, as on a decode error. The frame was decoded from the
state before the first remembered frame, the nearest state the decoder
keeps. Only its decoder state is dropped, as the Coordinator's note on the
issue asks.

Every frame inside the window takes the same path as before, so no suite
scenario and no compared cell changes (`testbench_compare.py` reports no
change; `testbench/baseline/` gains only the new scenario's file).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_533_opus_decoder_frame_older_than_window.scn` (base built with `-DSM_SANITIZE=ON`) | ASan `heap-use-after-free` in `OpusAudioDecoder::Insert`, exit 1, seeds 1-5 | runs to the end, `audio.concealed_pct` 32-36 (expect < 100), seeds 1-5 |
| `tests/unit/testbench/opus_decoder_test.cc` `OpusDecoder.FrameOlderThanTheWindowIsDecodedNotKept` | ASan `heap-use-after-free` (ASan build) | passes: 960 samples returned, and the next frame decodes as in order |

The native build passes the scenario before the fix too (it reads the freed
block without noticing), so the scenario names
`# fix-record-base-cmake: -DSM_SANITIZE=ON` and the `fix-record` job builds
the base commit with ASan+UBSan, as for #441.

## Results

The reproduction scenario across seeds, ASan+UBSan build (before -> after):

| seed | before | after (`audio.concealed_pct`) |
|---|---|---|
| 1 | abort, heap-use-after-free | runs, 36.27 |
| 2 | abort, heap-use-after-free | runs, 32.14 |
| 3 | abort, heap-use-after-free | runs, 34.03 |
| 4 | abort, heap-use-after-free | runs, 33.92 |
| 5 | abort, heap-use-after-free | runs, 35.9 |

The scenario suite against the baseline (`tools/testbench_compare.py`):
no metric changed in any existing scenario; the new scenario is added to
`testbench/baseline/`.
