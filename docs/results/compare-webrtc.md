# streaming_media vs libwebrtc

Every scenario in [testbench/scenarios](../../testbench/scenarios) was run
through this library and through libwebrtc M157 (prebuilt, default field
trials) over the same emulated links, with the same encoder model. See
[compare/README.md](../../compare/README.md) for the method and its
limits. Each value is the mean of 3 seeds. Raw numbers:
[compare-webrtc.json](compare-webrtc.json).

Regenerate with:

```sh
compare/webrtc/fetch.sh
cmake -S compare/webrtc -B out/compare -DCMAKE_BUILD_TYPE=Release && cmake --build out/compare -j
out/compare/sm_compare --json=docs/results/compare-webrtc.json testbench/scenarios/*.scn
```

Measured on 2026-10-14 at commit 415a29c plus the comparison tool, macOS arm64.

## Results

Each cell is **streaming_media / libwebrtc**. For utilization and render
fps higher is better; for every other column lower is better.

| Scenario | Audio concealed % | Mouth-to-ear p95 ms | Frozen ms | Render fps | Video delay p95 ms | Keyframe requests | Utilization % | Queue p95 ms | Link loss % | Repair overhead % |
|---|---|---|---|---|---|---|---|---|---|---|
| audio_only_lossy | 1.7 / 0.4 | 109 / 145 | - | - | - | - | 90.3 / 74.3 | 3.3 / 3.0 | 9.8 / 10.0 | 0 / 0 |
| bufferbloat | 0.3 / 0.2 | 91.9 / 184 | 0 / 0 | 30.0 / 29.9 | 72.7 / 96.2 | 1 / 0 | 87.2 / 92.1 | 28.3 / 48.6 | 0 / 0 | 0 / 0.1 |
| burst_loss | 2.8 / 1.7 | 124 / 281 | 1279 / 1394 | 26.8 / 29.9 | 214 / 214 | 18 / 0 | 50.2 / 83.2 | 15.9 / 57.3 | 3.0 / 2.7 | 41.1 / 4.3 |
| capacity_drop | 1.3 / 1.0 | 789 / 795 | 4325 / 2355 | 26.4 / 28.9 | 325 / 149 | 13 / 0 | 54.3 / 57.6 | 387 / 271 | 4.7 / 1.4 | 11.1 / 2.9 |
| clean_link | 0.4 / 0.2 | 88.4 / 227 | 0 / 0 | 29.9 / 29.9 | 78.3 / 120 | 1 / 0 | 86.4 / 91.0 | 23.3 / 66.6 | 0 / 0 | 0 / 0.1 |
| cross_traffic | 0.8 / 0.6 | 281 / 669 | 0 / 2924 | 30.0 / 29.9 | 177 / 703 | 1 / 0 | 72.1 / 73.1 | 136 / 261 | 0 / 0.5 | 0 / 0.8 |
| high_jitter | 1.2 / 0.1 | 187 / 220 | 3395 / 0 | 18.2 / 29.7 | 1558 / 142 | 57 / 0.3 | 8.8 / 39.0 | 1.2 / 5.2 | 0 / 0 | 24.9 / 44.0 |
| lossy_feedback | 0.8 / 0.3 | 98.0 / 204 | 0 / 0 | 29.9 / 29.9 | 85.8 / 112 | 1 / 0 | 82.1 / 85.9 | 22.7 / 41.7 | 0 / 0 | 0 / 0.1 |
| low_bandwidth | 3.2 / 1.8 | 912 / 887 | 21901 / 14304 | 5.1 / 21.7 | 1878 / 4087 | 84.3 / 0.3 | 99.2 / 98.8 | 498 / 485 | 25.0 / 6.6 | 136 / 16.2 |
| outage_3s | 11.1 / 10.2 | 613 / 176 | 9409 / 3217 | 20.7 / 26.8 | 172 / 112 | 12.3 / 1 | 59.5 / 89.6 | 294 / 38.8 | 16.2 / 5.9 | 25.8 / 0.5 |
| random_loss_5pct | 1.7 / 0.4 | 142 / 157 | 2107 / 1793 | 26.8 / 29.9 | 402 / 194 | 12.7 / 0 | 35.4 / 76.0 | 8.8 / 26.0 | 5.0 / 4.8 | 64.1 / 8.7 |
| satellite | 0.8 / 0.3 | 367 / 566 | 6434 / 16238 | 24.5 / 29.4 | 722 / 930 | 15.3 / 0.3 | 42.6 / 82.9 | 11.8 / 57.7 | 0.9 / 0.9 | 30.4 / 17.7 |
| wifi_to_lte_handover | 4.6 / 4.2 | 457 / 545 | 7174 / 4702 | 17.7 / 28.0 | 172 / 2173 | 28.7 / 0 | 86.7 / 105 | 194 / 173 | 34.8 / 5.6 | 101 / 33.8 |

## Where libwebrtc is ahead

1. **Keyframe requests (a test bench bug, #18).** The test bench requests
   12-84 keyframes per call in lossy scenarios, where libwebrtc requests
   0-1 and recovers with NACK. Most of these requests come from the test
   bench itself: it sends every loss notification (LNTF) to the sender as
   a PLI. Without that, burst_loss drops from 18 requests to 1 and from
   1.3 s to 0.1 s of freezes, and random_loss_5pct goes from 35% to 60%
   utilization with no freezes. The numbers in this table include the bug.
2. **Bitrate under loss.** At 5% random loss utilization is 35% vs 76%
   (60% vs 76% without #18), and video is 375 vs 1355 kbps. The satellite
   link shows a similar gap (43% vs 83%). See #6, #7 and #12.
3. **High jitter (#19).** With 40 ms jitter and reordering, the target
   drops to about 160 kbps on a 3 Mbps link, and the video delay p95 is
   1.5 s. libwebrtc has 1 Mbps and 142 ms.
4. **Recovery after outages and handovers (#20).** After a 3 s outage the
   queue p95 is 294 ms vs 39 ms, freezes total 9.4 s vs 3.2 s, and
   mouth-to-ear p95 is 613 ms vs 176 ms. After the Wi-Fi to LTE handover,
   35% of packets are lost vs 5.6%.
5. **Overload (#21).** At 150 kbps, retransmissions, FEC and keyframes
   fill the link while the target is only about 41 kbps. The result is 26%
   loss, 5 fps and 2 audio interruptions, against 6.6% loss, 22 fps and
   none for libwebrtc. The 5x capacity drop shows the same pattern on a
   smaller scale. Slow recovery afterwards is #14 and #15.
6. **Audio concealment under loss** is lower in libwebrtc (0.4% vs 1.7%
   at 10% loss). Part of this comes from Opus in-band FEC, which this
   library does not have.

## Where streaming_media is ahead

1. **Audio latency.** The jitter buffer holds less audio on every
   scenario: mouth-to-ear p95 is 88 ms vs 227 ms on the clean link and
   124 ms vs 281 ms under burst loss. libwebrtc's NetEq grows its buffer
   in response to the queueing that video bursts cause at the
   bottleneck. In an audio-only call on a clean link it holds about
   22 ms, so the growth is a reaction to video, not a fixed floor.
2. **Queueing on stable links.** Queue p95 on the clean link is 23 ms vs
   67 ms, at a slightly lower utilization (86% vs 91%).
3. **Competing traffic.** When a 1.5 Mbps flow joins, this library has no
   freezes and a 177 ms video delay p95. libwebrtc freezes for 2.9 s,
   reaches 703 ms, and takes 5 s to settle.
4. **Satellite freezes.** On the satellite link: 6.4 s frozen vs 16.2 s,
   at a much lower bitrate.
