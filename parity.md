## Parity with libwebrtc M157.8103@{#0}

172 scenarios, mean of 10 seeds. **69 gaps**: 0 behind libwebrtc, 69 ahead of it; 54 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `cc.max_settle_ms` | 1340 | 14630 | ±798 | ahead | 80/80 | **none** (closed, no note: #15, #48, #61, #20) |
| capacity_drop | `cc.utilization_pct` | 82.8 | 54.8 | ±4.1 | ahead | 80/80 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| capacity_drop | `video.avg_kbps` | 1330 | 855 | ±66.5 | ahead | 80/80 | **none** (closed, no note: #37, #61) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 300 | 1006 | ±211 | ahead | 10/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 343 | 1047 | ±211 | ahead | 10/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 17.2 | 13.4 | ±2 | ahead | 8/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #875) |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.7 | 12.5 | ±1.6 | ahead | 10/10 | #479 (closed, no note: #21, #61, #12, #860, #875) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 32.6 | 62.5 | ±11.6 | ahead | 10/10 | #479 (closed, no note: #21, #7, #29, #860, #875) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 1887 | 3445 | ±385 | ahead | 9/10 | #741 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 189 | 138 | ±20 | ahead | 9/10 | **none** (closed, no note: #37, #61, #875) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2051 | 4019 | ±497 | ahead | 9/10 | #479 (closed, no note: #24, #37) |
| issue_14_capacity_recovery | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_14_capacity_recovery | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_14_capacity_recovery | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 4774 | 8701 | ±2334 | ahead | 78/80 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_204_cross_traffic_join | `cc.utilization_pct` | 70.5 | 57.6 | ±3.5 | ahead | 79/80 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_204_cross_traffic_join | `video.avg_kbps` | 1689 | 1367 | ±84.4 | ahead | 79/80 | **none** (closed, no note: #37, #61) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_23_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_23_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_23_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_346_reconfigure_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_346_reconfigure_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_346_reconfigure_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_346_reconfigure_cross_join | `cc.max_settle_ms` | 4774 | 8701 | ±2334 | ahead | 78/80 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_346_reconfigure_cross_join | `cc.utilization_pct` | 70.5 | 57.6 | ±3.5 | ahead | 79/80 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_346_reconfigure_cross_join | `video.avg_kbps` | 1689 | 1367 | ±84.4 | ahead | 79/80 | **none** (closed, no note: #37, #61) |
| issue_356_capacity_drop_xr_rtt | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_356_capacity_drop_xr_rtt | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_356_capacity_drop_xr_rtt | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_390_cross_join_seed15 | `cc.utilization_pct` | 69.7 | 58.2 | ±3.5 | ahead | 9/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_390_cross_join_seed15 | `video.avg_kbps` | 1670 | 1383 | ±83.5 | ahead | 9/10 | **none** (closed, no note: #37, #61) |
| issue_536_loss20_av_sync | `audio.jb_delay_p95_ms` | 176 | 665 | ±449 | ahead | 9/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_loss20_av_sync | `audio.mouth_to_ear_p95_ms` | 240 | 728 | ±447 | ahead | 9/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_loss20_av_sync | `sync.av_offset_p95_ms` | 355 | 1173 | ±652 | ahead | 10/10 | #536, #729 |
| issue_536_loss20_av_sync | `video.e2e_delay_p95_ms` | 512 | 1776 | ±1002 | ahead | 10/10 | #536, #729 (closed, no note: #24, #37) |
| issue_536_reverse_outage_45s | `audio.jb_delay_p95_ms` | 221 | 1978 | ±98.9 | ahead | 10/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_reverse_outage_45s | `audio.mouth_to_ear_p95_ms` | 312 | 2070 | ±104 | ahead | 10/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_reverse_outage_45s | `sync.av_offset_p95_ms` | 147 | 1595 | ±81.9 | ahead | 10/10 | #536, #729 |
| issue_536_reverse_outage_45s | `video.e2e_delay_p95_ms` | 324 | 2307 | ±115 | ahead | 10/10 | #536, #729 (closed, no note: #24, #37) |
| issue_5_keyframe_flush | `cc.max_settle_ms` | 1300 | 14990 | ±750 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_5_keyframe_flush | `cc.utilization_pct` | 82.8 | 54.1 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_5_keyframe_flush | `video.avg_kbps` | 1330 | 845 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_76_rtx_v2_keyframe_flush | `cc.max_settle_ms` | 9370 | 14720 | ±736 | ahead | 9/10 | #875 (closed, no note: #15, #48, #61, #20) |
| issue_76_rtx_v2_keyframe_flush | `cc.utilization_pct` | 59.1 | 51.2 | ±3.0 | ahead | 8/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 932 | 796 | ±46.6 | ahead | 8/10 | **none** (closed, no note: #37, #61) |
| issue_795_loss20_mouth_to_ear | `audio.jb_delay_p95_ms` | 176 | 665 | ±449 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 240 | 728 | ±447 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `sync.av_offset_p95_ms` | 355 | 1173 | ±652 | ahead | 10/10 | **none** |
| issue_795_loss20_mouth_to_ear | `video.e2e_delay_p95_ms` | 512 | 1776 | ±1002 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_795_reverse_outage_mouth_to_ear | `audio.jb_delay_p95_ms` | 221 | 1978 | ±98.9 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 312 | 2070 | ±104 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `sync.av_offset_p95_ms` | 147 | 1596 | ±82.3 | ahead | 10/10 | **none** |
| issue_795_reverse_outage_mouth_to_ear | `video.e2e_delay_p95_ms` | 324 | 2307 | ±115 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_911_host_rtt_10s | `cc.max_settle_ms` | 4830 | 12000 | ±600 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20, #911) |
| issue_911_host_rtt_10s | `cc.utilization_pct` | 74.2 | 60.5 | ±3.7 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #911) |
| issue_911_host_rtt_10s | `video.avg_kbps` | 1510 | 1216 | ±75.5 | ahead | 10/10 | **none** (closed, no note: #37, #61, #911) |
| issue_921_host_rtt_under_limit | `cc.max_settle_ms` | 4830 | 12000 | ±600 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20, #921) |
| issue_921_host_rtt_under_limit | `cc.utilization_pct` | 74.2 | 60.5 | ±3.7 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #921) |
| issue_921_host_rtt_under_limit | `video.avg_kbps` | 1510 | 1216 | ±75.5 | ahead | 10/10 | **none** (closed, no note: #37, #61, #921) |
| pacer_v2_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| pacer_v2_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| pacer_v2_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| probe_v2_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| probe_v2_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `video.avg_kbps` | 1330 | 855 | ±66.5 | ahead | 80/80 | **none** (closed, no note: #37, #61) |
| capacity_drop | `cc.utilization_pct` | 82.8 | 54.8 | ±4.1 | ahead | 80/80 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| capacity_drop | `cc.max_settle_ms` | 1340 | 14630 | ±798 | ahead | 80/80 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 189 | 138 | ±20 | ahead | 9/10 | **none** (closed, no note: #37, #61, #875) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 17.2 | 13.4 | ±2 | ahead | 8/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #875) |
| issue_14_capacity_recovery | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_14_capacity_recovery | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_14_capacity_recovery | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_204_cross_traffic_join | `video.avg_kbps` | 1689 | 1367 | ±84.4 | ahead | 79/80 | **none** (closed, no note: #37, #61) |
| issue_204_cross_traffic_join | `cc.utilization_pct` | 70.5 | 57.6 | ±3.5 | ahead | 79/80 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 4774 | 8701 | ±2334 | ahead | 78/80 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_23_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_23_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_23_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_346_reconfigure_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_346_reconfigure_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_346_reconfigure_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_346_reconfigure_cross_join | `video.avg_kbps` | 1689 | 1367 | ±84.4 | ahead | 79/80 | **none** (closed, no note: #37, #61) |
| issue_346_reconfigure_cross_join | `cc.utilization_pct` | 70.5 | 57.6 | ±3.5 | ahead | 79/80 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_346_reconfigure_cross_join | `cc.max_settle_ms` | 4774 | 8701 | ±2334 | ahead | 78/80 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_356_capacity_drop_xr_rtt | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_356_capacity_drop_xr_rtt | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_356_capacity_drop_xr_rtt | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_390_cross_join_seed15 | `video.avg_kbps` | 1670 | 1383 | ±83.5 | ahead | 9/10 | **none** (closed, no note: #37, #61) |
| issue_390_cross_join_seed15 | `cc.utilization_pct` | 69.7 | 58.2 | ±3.5 | ahead | 9/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_5_keyframe_flush | `video.avg_kbps` | 1330 | 845 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| issue_5_keyframe_flush | `cc.utilization_pct` | 82.8 | 54.1 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_5_keyframe_flush | `cc.max_settle_ms` | 1300 | 14990 | ±750 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 932 | 796 | ±46.6 | ahead | 8/10 | **none** (closed, no note: #37, #61) |
| issue_76_rtx_v2_keyframe_flush | `cc.utilization_pct` | 59.1 | 51.2 | ±3.0 | ahead | 8/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_795_loss20_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 240 | 728 | ±447 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `audio.jb_delay_p95_ms` | 176 | 665 | ±449 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `video.e2e_delay_p95_ms` | 512 | 1776 | ±1002 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_795_loss20_mouth_to_ear | `sync.av_offset_p95_ms` | 355 | 1173 | ±652 | ahead | 10/10 | **none** |
| issue_795_reverse_outage_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 312 | 2070 | ±104 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `audio.jb_delay_p95_ms` | 221 | 1978 | ±98.9 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `video.e2e_delay_p95_ms` | 324 | 2307 | ±115 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_795_reverse_outage_mouth_to_ear | `sync.av_offset_p95_ms` | 147 | 1596 | ±82.3 | ahead | 10/10 | **none** |
| issue_911_host_rtt_10s | `video.avg_kbps` | 1510 | 1216 | ±75.5 | ahead | 10/10 | **none** (closed, no note: #37, #61, #911) |
| issue_911_host_rtt_10s | `cc.utilization_pct` | 74.2 | 60.5 | ±3.7 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #911) |
| issue_911_host_rtt_10s | `cc.max_settle_ms` | 4830 | 12000 | ±600 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20, #911) |
| issue_921_host_rtt_under_limit | `video.avg_kbps` | 1510 | 1216 | ±75.5 | ahead | 10/10 | **none** (closed, no note: #37, #61, #921) |
| issue_921_host_rtt_under_limit | `cc.utilization_pct` | 74.2 | 60.5 | ±3.7 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #921) |
| issue_921_host_rtt_under_limit | `cc.max_settle_ms` | 4830 | 12000 | ±600 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20, #921) |
| pacer_v2_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| pacer_v2_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| pacer_v2_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |
| probe_v2_capacity_drop | `video.avg_kbps` | 1331 | 850 | ±66.5 | ahead | 10/10 | **none** (closed, no note: #37, #61) |
| probe_v2_capacity_drop | `cc.utilization_pct` | 82.8 | 54.4 | ±4.1 | ahead | 10/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 1310 | 14870 | ±744 | ahead | 10/10 | **none** (closed, no note: #15, #48, #61, #20) |

