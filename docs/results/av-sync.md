# A/V sync against libwebrtc (issue #63, section 8)

`sm_compare` (10 seeds, means) for PR #237 @ 2d59db6, as published to
`parity-data-pr/pr/237/`. CI's `compare-webrtc` job regenerates the numbers on
every `main` run (`parity-data`, `parity.md`).

**Metric.** At each rendered video frame, the offset is the capture time of
the audio playing at that moment minus the frame's capture time. A positive
offset means the video is late. This is libwebrtc's
`RtpStreamsSynchronizer::GetStreamSyncOffsetInMs`
(`video/rtp_streams_synchronizer2.cc`, the `sync_offset_ms` stat), with exact
capture times instead of RTCP sender-report estimates (`testbench::AvSyncMeter`).
The audio playout point is read in two places:

- libwebrtc: `Syncable::GetPlayoutRtpTimestamp`;
- ours: `AudioJitterBuffer::PlayoutTimestamp()`, as `NetEq::GetPlayoutTimestamp`.

**Neither stack synchronizes here.** libwebrtc's receive streams have no
`sync_group`, and the test bench has no A/V sync yet (#77). So these numbers
are the raw offset of each stack's audio and video receive paths: video
delay minus audio delay. A call that puts both tracks in one sync group
would move them toward 0 within libwebrtc's 30 ms tolerance
(`stream_synchronization.cc`).

## Suite scenarios

| scenario | p50 ours | p50 libwebrtc | p95 ours | p95 libwebrtc | p95 band | p95 vs libwebrtc |
|---|---|---|---|---|---|---|
| clean_link | -12 | -54 | 63 | 79 |  | in band |
| bufferbloat | -7 | -54 | 50 | 73 | ±17.7 | ahead |
| lossy_feedback | -5 | -49 | 48 | 110 | ±27.3 | ahead |
| random_loss_5pct | 185 | 53 | 199 | 85 | ±15.0 | behind |
| burst_loss | 91 | 62 | 111 | 102 |  | in band |
| high_jitter | 67 | -15 | 97 | 58 | ±18.0 | behind |
| capacity_drop | 71 | -90 | 184 | 286 | ±33.0 | ahead |
| cross_traffic | 4 | -113 | 186 | 259 | ±62.6 | ahead |
| outage_3s | 3 | -39 | 79 | 72 |  | in band |
| wifi_to_lte_handover | 112 | 38 | 657 | 1832 | ±467 | ahead |
| satellite | 415 | 123 | 521 | 497 |  | in band |
| low_bandwidth | 1770 | 1989 | 1993 | 2487 | ±237 | ahead |
| very_low_capacity | 2190 | 3362 | 5794 | 7211 |  | in band |

`sync.av_offset_p95_ms` is a gap on 51 of the 70 scenarios with audio and video:
we are ahead (smaller offset) on 29 and behind on 22. The parity
report puts them under #77.

## What the numbers show

- **On clean and congested links, libwebrtc's audio plays later than its video.**
  The median is −54 ms on `clean_link` and −113 ms on `cross_traffic`; ours
  is −12 and +5 ms. The difference is on the audio side:
  - `clean_link`: video e2e p95 is 120 vs 123 ms, but audio jitter-buffer
    p95 is 85 vs 130 ms.
  - `cross_traffic`: audio jitter-buffer p95 is 182 vs 382 ms.

  #62 (NetEq as the default audio receive path, #236) owns that difference.
- **Under random loss and shallow queues, our video is later.** The median
  is +185 vs +53 ms on `random_loss_5pct`, and +252 vs +55 ms on
  `issue_12_lossbwe_shallow`. The difference is on the video side: video
  e2e p95 is 292 vs 224 ms and 321 vs 226 ms, while audio mouth-to-ear p95
  is within 20 ms. #24 (render timing) and #230 (repair latency) own that.
- **At very low capacity**, both stacks' video runs seconds behind
  (`very_low_capacity` p50 2.2 s vs 3.4 s). This is send-side frame dropping
  and queueing, not sync.

## Approximations

- libwebrtc's audio playout point is read once per 10 ms pull. Its time is
  when ChannelReceive last sampled NetEq, as in libwebrtc's own sync, so its
  resolution is one pull.
- Audio capture times assume the first RTP timestamp sent is the first
  sample captured at t = 0, as `audio.net_delay_p95_ms` does on both sides.

