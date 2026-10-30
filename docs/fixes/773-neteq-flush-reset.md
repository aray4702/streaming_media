---
issue: 773
title: "NetEq: reset the delay manager after a full-buffer flush, as libwebrtc's new_codec_ path (kFlushed)"
severity: sev:3
status: fixed
area: area:timing, area:audio
regression_tests: testbench/scenarios/issue_773_neteq_flush_reset.scn, tests/unit/timing/audio_jitter_buffer_test.cc
---

# #773: NetEq: reset the delay manager after a full-buffer flush, as libwebrtc's new_codec_ path (kFlushed)

## Issue

When NetEq's packet buffer fills (200 packets) and is flushed, libwebrtc M157
soft-resets its controller at the next decision. Ours kept the old delay
manager state, so after a jittery stretch that ends in a flush the audio stayed
seconds behind once the link calmed. https://github.com/aray4702/streaming_media/issues/773,
found by the Fixer on #720 (comparison on #504), `main` 884a7ce.

## Root cause

`AudioJitterBuffer::Insert` (`src/timing/audio_jitter_buffer.cc:117-127` on
695dc71) flushed the queue, moved `next_ts_` to the new packet, reset the sync
buffer and called `decision_.NotifyBufferFlush()`. It did not take libwebrtc's
`new_codec_` path:

- `PacketBuffer::InsertPacket` returns `kFlushed` (`packet_buffer.cc:162-177`)
  and `NetEqImpl::InsertPacketInternal` sets `new_codec_`
  (`neteq_impl.cc:369-372`);
- until the next decision, packets skip the controller's statistics
  (`should_update_stats = !new_codec_`, `:387-389`) and `GetDecision` skips
  `DiscardOldPackets` (`:915`);
- after the early return for a sync buffer that already holds an output block
  (`:1019-1025`), `GetDecision` restarts the timestamp at the next packet,
  makes the operation `kNormal` and calls `controller_->SoftReset()`
  (`:1034-1061`), which resets `DelayManager` (target 80 ms), the buffer level
  filter and the packet arrival history (`decision_logic.cc:88-98`).

Ours kept feeding the flushed burst into the delay manager, whose target and
underrun histogram then held the audio delay high. libwebrtc also leaves the
sync buffer alone on `kFlushed` at the same sample rate (`SetSampleRateAndChannels`
runs only when the rate changes, `:407-430`); ours reset it.

## Fix

A `neteq_flush_pending_` flag (libwebrtc's `new_codec_` for `kFlushed`):

- the flush check runs before the duplicate check, as in `PacketBuffer::InsertPacket`,
  and `PacketArrived` runs after it, only while no reset is pending;
- `DiscardOldPackets` is skipped while the flag is set;
- in `GetAudioV2`, after the sync-buffer early return, the pending reset moves
  `next_ts_` to the next packet, makes the operation normal and soft-resets
  `NetEqDelayManager` and `NetEqDecisionLogic` (event `neteq/flush_reset`);
- the sync buffer and its timestamp are kept until then; the first-packet
  reset clears the same flag.

`ResetMcu` (statistics bookkeeping for the old MCU report) has no counterpart here.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_773_neteq_flush_reset.scn` | `audio.jb_delay_p50_ms` 2020 (expect < 1000) | 580 |
| `tests/unit/timing/audio_jitter_buffer_test.cc` `AudioJitterBuffer.V2FullBufferFlushSoftResetsTheDelayManagerAtTheNextDecision` | target 200 ms after the flush and 50 pulls (expect 80) | target 80 ms within 30 pulls, playout at the kept packets |

## Results

The reproduction scenario across seeds (before -> after):

| seed | metric | before | after |
|---|---|---|---|
| 1 | audio.jb_delay_p50_ms | 2020 | 580 |
| 2 | audio.jb_delay_p50_ms | 1750 | 300 |
| 3 | audio.jb_delay_p50_ms | 710 | 70 |
| 4 | audio.jb_delay_p50_ms | 1190 | 70 |
| 5 | audio.jb_delay_p50_ms | 390 | 130 |

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):
no metric changed on any existing scenario (0 regressions, 0 improvements;
`receiver.neteq.buffer_flushes` is 0 on all of them); the new scenario adds one
baseline file.
