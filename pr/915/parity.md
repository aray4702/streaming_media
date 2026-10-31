## Parity with libwebrtc M157.8103@{#0}

157 scenarios, mean of 10 seeds. **31 gaps**: 15 behind libwebrtc, 16 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

31 gaps on `main`, 31 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 686 | 1006 | ±211 | ahead | 7/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 728 | 1047 | ±211 | ahead | 7/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.1 | 12.5 | ±1.6 | ahead | 9/10 | #479 (closed, no note: #21, #61, #12, #860, #875) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 48.3 | 62.5 | ±11.6 | ahead | 8/10 | #479 (closed, no note: #21, #7, #29, #860, #875) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2664 | 3445 | ±385 | ahead | 8/10 | #741 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2974 | 4019 | ±497 | ahead | 8/10 | #479 (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 96 | ±10 | ahead | 6/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 151 | ±10 | ahead | 6/10 | #411, #479 (closed, no note: #63, #476) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1935 | 1460 | ±400 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.3 | 78.6 | ±11.1 | behind | 6/10 | #861 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.3 | 78.6 | ±11.1 | behind | 6/10 | #861 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1935 | 1460 | ±400 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739, #479 (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.2 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 289 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1208 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1560 | 1979 | ±250 | ahead | 9/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1891 | 2311 | ±250 | ahead | 9/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3222 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5945 | 4944 | ±964 | behind | 5/10 | #466, #479 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 8.8 | 7.2 | ±1.4 | ahead | 7/10 | #466, #479 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4187 | 1245 | ±2661 | behind | 5/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_867_nack_reorder_loss | `sync.av_offset_p95_ms` | 132 | 97.1 | ±27.7 | behind | 7/10 | #870 |
| issue_867_nack_reorder_loss | `video.avg_kbps` | 223 | 255 | ±25.9 | behind | 6/10 | #870 (closed, no note: #37, #61) |
| issue_867_nack_reorder_loss | `video.e2e_delay_p95_ms` | 330 | 302 | ±21.8 | behind | 5/10 | #870 (closed, no note: #24, #37) |
| very_low_capacity | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |

