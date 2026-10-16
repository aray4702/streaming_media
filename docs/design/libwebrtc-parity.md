# Parity with libwebrtc M157: deviation inventory

Stage 1 of the program duplicates libwebrtc (`program.md` 0.1). This
document lists every place where this library differs from libwebrtc at the
pinned commit (M157, `b321f634929af0af61a8964487fbd7894a008f12`), area by
area, so that stage-1 work is driven by the differences and not only by the
symptoms they cause. It is the deviation inventory of issue #59. Reference
sources are the files under `compare/webrtc/reference/`; paths below are
relative to that directory.

**Status of each row**

- `port`: libwebrtc's behaviour is to be duplicated. The last column names
  the roadmap issue (from #64) that does it, or `unplaced` for the Owner to
  place.
- `deviation (approved, #N)`: a difference the Owner has accepted. None are
  recorded yet; a row can move here only on an Owner decision.
- `n/a`: no libwebrtc counterpart or not in scope, with the reason.
- `same`: checked and equal (listed where a reader would otherwise wonder).

**Scope of this version.** Section 5 (congestion control) only, as issue #59
asks for cc first. Sections 3, 4, 6, 7, 8 and the RTP session layer follow in
separate PRs; their headings are below with the state `not yet inventoried`.
Rows were found by reading our code next to libwebrtc's. A constant listed as
`same` was compared; a module not listed has not been compared yet. No code
is changed by this document.

## 5. Congestion control

### 5.1 Transport-wide feedback

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `cc/transport_feedback.cc` `SendHistory`, `PacketResult` (send history joined with feedback) | `api/transport/network_types.h` (`PacketResult`, `TransportPacketsFeedback`); libwebrtc's `TransportFeedbackAdapter` is not in `reference/` | Ours drops history entries after 60 s. libwebrtc's adapter (feedback ordering, in-flight accounting, packets of lost feedback) is not in the reference set, so it cannot be compared. | cannot compare: file needed | list on #57 |
| `cc/transport_feedback.h` `TransportFeedback` (parse/serialise of the transport-cc message) | `modules/remote_bitrate_estimator/transport_sequence_number_feedback_generator.*`, `congestion_control_feedback_generator.*` (RFC 8888 variant) | Wire format is the draft-holmer message, interop-checked in `compare/webrtc/rtcp_interop.cc`. libwebrtc's receiver-side feedback generator (send interval, which packets are reported) is a separate component; ours is in `src/rtp`. Interval and content not compared here. | unplaced (belongs with the RTP session inventory) | unplaced |
| REMB (`GccController::OnRemb`) | `modules/remote_bitrate_estimator/remote_bitrate_estimator_*.cc`, `send_side_bandwidth_estimation.cc` (`receiver_limit_`, `GetUpperLimit`) | libwebrtc applies the REMB value as `receiver_limit_` in the upper limit of `SendSideBandwidthEstimation`; ours takes `min(target, remb)` after the other estimates. Equivalent in effect, different place. | port with #61 | #61 |

### 5.2 Delay-based estimator

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `InterArrival` (`cc/delay_based_bwe.cc`) | `goog_cc/inter_arrival_delta.cc` | Group boundary: libwebrtc starts a new group when the send time is more than 5 ms after the group's first packet, **unless** the packet belongs to a burst (`BelongsToBurst`: arrival delta <= 5 ms with a negative propagation delta, group shorter than 100 ms; `kBurstDeltaThreshold`, `kMaxBurstDuration`). Ours uses only the 5 ms send-time span. Missing: reordered-packet handling (`kReorderedResetThreshold` 3), the arrival-time-offset reset (`kArrivalTimeOffsetThreshold`), and the negative-delta checks. | port | #19 |
| `TrendlineEstimator` | `goog_cc/trendline_estimator.cc` | Constants **same**: window 20, smoothing 0.9, gain 4.0, `k_up` 0.0087, `k_down` 0.039, initial threshold 12.5, 10 ms overuse time, clamp 6..600, `kMaxAdaptOffsetMs` 15, `min(num_deltas, 60)`. Differences: (1) the overuse transition conditions are the same (`trend >= prev_trend`, 10 ms, counter > 1); (2) libwebrtc also has field-trial variants (sorting, capped delay, packet-size-based threshold gain; `TrendlineEstimatorSettings`) that were not read for this table. Treat as equal until #19 shows otherwise; keep as a checklist item. | same (re-check in #19) | #19 |
| Delay detector wiring (`DelayBasedBwe::OnPacketResults`) | `goog_cc/delay_based_bwe.cc` | (1) libwebrtc has a separate audio/video detector pair (`WebRTC-Bwe-SeparateAudioPackets`, off by default) and a 2 s stream timeout (`kStreamTimeOut`) that resets the detector; ours has neither (the timeout matters after an outage). (2) When overusing **without an acked bitrate**, libwebrtc halves the estimate every 200 ms (`InitialTimeToReduceFurther`); ours does not act. (3) When not overusing and a probe result exists, libwebrtc sets the estimate to the probe bitrate unconditionally (`rate_control_.SetEstimate`); ours adopts it only when it is above the current estimate. (4) libwebrtc resets the inter-arrival state and trendline after a `kStreamTimeOut` gap and after `recovered_from_overuse`; ours does not. | port | #19 (1, 4), #15 (2), #48 (3) |
| `AcknowledgedBitrateEstimator` | `goog_cc/acknowledged_bitrate_estimator.cc`, `bitrate_estimator.cc` | Ours: 500 ms sliding window of received bytes, needs >= 100 ms span, smoothed 0.7/0.3. libwebrtc: `BitrateEstimator`, 500 ms window for the first sample then 150 ms (`kInitialRateWindowMs`, `kMinRateWindowMs`), a Bayesian (Kalman-like) estimate with `uncertainty_scale` 10 and initial variance 50, and the sample dropped while the estimate is unset. Different filter, different window, different response after a rate change. | port | #15 |
| ALR detection (`GccController::OnProcessInterval`: sending < 65% of the target) | `goog_cc/alr_detector.cc` | libwebrtc uses an `IntervalBudget` fed by the **pacer's** sent media: ALR starts when the budget ratio rises above 0.80 and ends below 0.50, with usage ratio 0.65. Ours compares an instantaneous send rate with the target, with no hysteresis. ALR also gates the acked-rate uncertainty and the AIMD increase limit in libwebrtc (`in_alr_`, `no_bitrate_increase_in_alr`); ours uses it only for probing. | port | #48 |
| `AimdRateController` | `modules/remote_bitrate_estimator/aimd_rate_control.cc` | Same: beta 0.85, increase limit 1.5 x throughput + 10 kbps, default RTT 200 ms, multiplicative increase `1.08^min(dt, 1 s)` with a 1000 bps floor, `TimeToReduceFurther` interval clamp(RTT, 10, 200 ms) with the "throughput < 0.5 x estimate" exception, hold after a decrease and return to increase on normal. Different: (1) additive increase: libwebrtc uses the near-max formula (30 fps frame size split into 1200 B packets, `response_time = 2 x (RTT + 100 ms)`, floor **4000 bps/s**); ours is one 1200 B packet per `(RTT + 100 ms)` with a 1000 bps floor. (2) Decrease: libwebrtc subtracts 5 kbps (when the result is > 5 kbps) and, if `beta x throughput` is above the current rate, falls back to `beta x link_capacity`; it clears the link-capacity estimate when throughput is below its lower bound, and records `last_decrease_` and the decrease time; ours does none of these and updates link capacity differently (see the next row). (3) The reduce-further guard in libwebrtc measures from the last bitrate change (`time_last_bitrate_change_`); ours measures from the last decrease. (4) libwebrtc has no estimate until the first overuse or `SetStartBitrate` (`bitrate_is_initialized_`) and the `kBitrateWindow`/`InitialTimeToReduceFurther` rule; ours seeds at 300 kbps. (5) `in_alr_` / `no_bitrate_increase_in_alr` and `network_estimate_` bounds are not implemented. | port | #15 |
| Link-capacity estimator (inside `AimdRateController`) | `goog_cc/link_capacity_estimator.cc` | Same constants: deviation clamp 0.4..2.5, `3 x deviation` bounds, `UpperBound`. Different: libwebrtc updates with `alpha = 0.05` on overuse (`OnOveruseDetected`) **and** with `alpha = 0.5` on a probe result (`OnProbeRate`); ours never takes the probe rate. Ours updates only at a decrease, in kbps, with the variance recomputed in a slightly different form (checked against `Update` in `link_capacity_estimator.cc`). | port | #15 |
| Start-up and `SetStartBitrate` (`DelayBasedBwe::SetStartBitrate`, `GccController::Start`) | `goog_cc/goog_cc_network_control.cc`, `delay_based_bwe.cc` | Our start rate is 300 kbps, max 10 Mbps, min 30 kbps from `GccConfig`. libwebrtc takes them from `TargetRateConstraints` set by the call (`SendSideBandwidthEstimation` uses `kDefaultMaxBitrate` 1 Gbps when no maximum is set). Our default max of 10 Mbps is a deviation that can clip high-rate scenarios; the start rate is not compared (the value comes from the call setup, which is not in `reference/`). | port (defaults) | #61 |

### 5.3 Probing

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `ProbeController` | `goog_cc/probe_controller.cc` | Same constants: first/second exponential probe 3x / 6x, further probe threshold 0.7, step 2x, ALR probe scale 2, min time between ALR probes 5 s. Different: probe duration (libwebrtc `initial_probe_duration` 100 ms for the start-up clusters, `min_probe_duration` 15 ms for later ones; ours 15 ms for all), minimum probe packets 5 (same). Missing: `kMaxWaitingTimeForProbingResult` 1 s, the probe-after-large-drop (`kBitrateDropThreshold` 0.66, 5 s timeout, fraction 0.85), probing after ALR ends (`kAlrEndedTimeout` 3 s), the network-availability probe, max-allocated-bitrate probes, and loss-limited probes (scale 1.5). **Periodic ALR probing is on by default here; libwebrtc enables it only when the stream asks for it (`requests_alr_probing`, `goog_cc_network_control.cc`).** | port | #48 |
| `ProbeBitrateEstimator` | `goog_cc/probe_bitrate_estimator.cc` | Same: >= 80% of probes and bytes received, 1 s maximum interval, unsaturated-link ratio 0.9, saturated-link factor 0.95. Missing: the `kMaxValidRatio` 2.0 check on the receive/send ratio and the `kMaxClusterHistory` 1 s expiry. The send/receive-interval size rule is applied the same way. | port | #48 |
| `Pacer` probe handling (`AddProbeCluster`) | `modules/pacing/bitrate_prober.cc`, `pacing_controller.cc` | libwebrtc's `BitrateProber` paces a cluster as a sequence of probes (`min_probe_delta` 2 ms, 20 ms for the initial clusters, `max_probe_delay` 10 ms, pending clusters time out after 5 s, at most 5 pending) and fills with padding. Ours sends a cluster in one burst at the probe rate with padding filler. | port | #48 |

### 5.4 Loss-based estimation and the combiner

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `LossBasedBwe` | `goog_cc/send_side_bandwidth_estimation.cc` (v1 loss rule, `UpdateEstimate`) | Same thresholds: low 2%, high 10%, decrease interval 300 ms + RTT, `target x (1 - 0.5 x loss)`. Differences: (1) below 2% libwebrtc **increases** by 1.08 x the minimum target of the last second + 1 kbps; ours imposes no limit. (2) libwebrtc measures fraction loss per RTCP report / transport-feedback report with `kLimitNumPackets` 20, not per 1 s window. (3) Start-phase handling (`kStartPhase` 2 s, `kBweConverganceTime` 20 s) is not in ours. (4) RTT back-off (`RttBasedBackoff`): its limit is infinite unless a field trial sets it, so it is off by default; ours has none (same). | port (replaced by v2) | #12 |
| Loss-based BWE v2 | `goog_cc/loss_based_bwe_v2.cc`, `.h` | Not implemented. libwebrtc M157 enables `LossBasedBweV2` through its field-trial defaults. | port | #12 |
| `GccController` target = min(delay, loss, REMB) | `goog_cc/goog_cc_network_control.cc`, `send_side_bandwidth_estimation.cc` | libwebrtc: delay-based estimate is an **upper limit** inside `SendSideBandwidthEstimation`; the loss-based v1/v2 result is the target; probe results, ALR, network state estimate and the cwnd pushback are applied around it in `GoogCcNetworkController`. Ours is a plain `min` of three numbers. | port (combine) | #61 |
| `GccController::ResetEstimate`, `OutageMonitor` (`cc/outage_monitor.cc`) | no counterpart (libwebrtc has `OnNetworkRouteChange` and `NetworkAvailability`, and `SendSideBandwidthEstimation` drops by 20% per `kMaxRtcpFeedbackInterval` 5 s without feedback) | Our `OutageMonitor` (degraded 600 ms, down 2 s, ICE restart request at 5 s, resume at 50% of the pre-outage rate, keyframe on resume, encoder pause) is not in libwebrtc. libwebrtc's no-feedback handling is the rule in `SendSideBandwidthEstimation::UpdateEstimate` (`kMaxRtcpFeedbackInterval`, `bwe_incoming`) and the 2 s detector timeout. | port or deviation: Owner decision needed | #20 |
| Pacing rate `target x 2.5` (`GccConfig::pacing_factor`) | `goog_cc_network_control.cc` (`kDefaultPaceMultiplier` 2.5 before the first transport feedback, `kDefaultPaceMultiplierWithSendSideBwe` **1.1** after) | Ours uses 2.5 always. | port | #61 |
| Congestion-window pushback | `goog_cc/congestion_window_pushback_controller.cc` | Ported in #56. Constants same (0.9, 0.95, 1.05, fill thresholds, 2-packet minimum, `min_pushback_rate`). | same | done (#53) |

### 5.5 Pacer

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `Pacer` | `modules/pacing/pacing_controller.cc`, `prioritized_packet_queue.cc`, `interval_budget.cc` | Different mechanism, same intent. Ours: a media-debt leaky bucket with a 5 ms burst allowance and a drain boost so no packet waits more than `max_queue_time` 2 s. libwebrtc: `PacingController` with a `send_burst_interval` of 40 ms by default (`PacerConfig::kDefaultTimeInterval`), `media_debt_` capped at `kMaxDebtInTime` 500 ms, `kMaxElapsedTime` 2 s, `kCongestedPacketInterval` 500 ms, padding at the padding rate with `kTargetPaddingDuration` 5 ms, and a large-queue drain (`MaybeUpdateMediaRateDueToLongQueue`, limit `kMaxExpectedQueueLength` 2 s, based on the **average** queue time, not the oldest packet). Priorities: libwebrtc 0 audio, 1 audio retransmission, 2 video retransmission, 3 video **and FEC together**, 4 padding; ours 0 audio, 1 retransmission, 2 video, 3 FEC, 4 padding, plus audio bypass of the budget. | port | unplaced (Phase 1 names RTX/FEC under the target, not the pacer itself) |
| Padding (`PacerConfig::padding_packet` 224 B) | `pacing_controller.cc` (`GeneratePadding`, `padding_debt_`, `kMaxPaddingReplayDuration` 50 ms); the padding packet size comes from `rtp_rtcp/source/rtp_sender.cc`, not in `reference/` | Ours pads to the probe or the padding rate with a fixed 224 B packet; libwebrtc sizes padding from the RTP sender and replays at most 50 ms of padding debt. Not compared in detail. | port (file needed) | list on #57 |

### 5.6 Allocation, frame dropping and adaptation (send-side media controller, #37)

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `FrameDropper` | `modules/video_coding/utility/frame_dropper.cc` | Ours is a leaky bucket on encoded bytes with a 500 ms window, keyframe bytes spread over 15 frames, cap 1.5 s. libwebrtc: `kLeakyBucketSizeSeconds` 0.5, accumulator cap `kAccumulatorCapBufferSizeSecs` **3 s**, filters (`kDefaultFrameSizeAlpha` 0.9, key-frame ratio alpha 0.99 with default 1/300, drop-ratio alpha 0.9 and value 0.96, `kDefaultMaxDropDurationSecs` 4 s), `large_frame_accumulation_spread_` and a rescale of the accumulator on a rate drop (`UpdateRatio`/`Fill`). The 1.5 s cap here is documented as a deviation in `include/sm/cc/frame_dropper.h` but is not approved. | port | #5 |
| `EncoderOvershootDetector` | `video/encoder_overshoot_detector.cc`, `video/encoder_bitrate_adjuster.cc` | Ours returns `target / utilization` over a 2 s window. libwebrtc's detector keeps a per-spatial-layer, per-temporal-layer overshoot estimate and an `EncoderBitrateAdjuster` that uses it. How it is enabled by default has not been compared. | port | #37 |
| `BitrateAllocator` | `call/bitrate_allocator.cc` (not in `reference/`), `modules/video_coding/svc/svc_rate_allocator.*`, `api/video/video_bitrate_allocation.h` | Ours is a water-filling allocator with a 10% resume hysteresis. libwebrtc's `BitrateAllocator` is not in the reference set. `call/bitrate_allocator.cc` is not in the reference set (`call/` has only the RTP transport controller and video sender), so the allocator cannot be compared. | port (file needed) | list on #57; place under #37 |
| `DegradationController`, `CpuOveruseDetector` (`cc/video_adaptation.cc`) | `video/adaptation/` (`bitrate_constraint`, `balanced_constraint`, `quality_scaler_resource`, `encode_usage_resource`, `video_stream_encoder_resource_manager`), `modules/video_coding/utility/quality_scaler.cc`, `bandwidth_quality_scaler.cc`, `video/video_stream_encoder.cc` | Ours picks a format from a bits-per-pixel budget (down 0.04, up 0.06) plus a CPU detector (85% / 42%, 5 s). libwebrtc adapts through resources: encode-usage (`OveruseFrameDetector`, 85/42 thresholds, checks every 5 s: same), quality scaler (QP thresholds per codec, `kMinFramesNeededToScale` 60 frames), bandwidth quality scaler, and `BitrateConstraint` (min bitrate per resolution from `GetEncoderBitrateLimits`). The bpp budget has no libwebrtc counterpart. The quality scaler is missing. | port | #37 |
| Pushback frame-drop interval | `video/video_stream_encoder.cc` (`UpdateTargetBitrate`, `FrameDropInterval`) | Ported in #56. | same | done (#53) |

### 5.7 Items with no libwebrtc counterpart or out of scope

| Our module | Reason | Status |
|---|---|---|
| `cc/outage_monitor.cc` ICE-restart request | ICE belongs to the host stack (`program.md` 0.1). Request signalling only. | n/a |
| Metrics / `SM_EVENT` hooks in every module | Library convention. | n/a |

## Not yet inventoried

These areas will be added in separate PRs for #59, in this order. Their
tables go in this file.

- 3 Packet loss (NACK, RTX, FEC / ULPFEC, RED, PLC, keyframe requests,
  protection selection)
- 4 Jitter and timing (packet buffer, RTCP statistics, delay manager, time
  stretch, audio jitter buffer, video jitter estimator, render timing)
- 6 Audio quality
- 7 Video quality
- 8 Synchronization
- RTP session layer (`src/rtp`)

## Open questions for the Owner

1. `OutageMonitor` (5.4): keep as a deviation, or port libwebrtc's
   no-feedback rule and drop it?
2. Pacer (5.5): not owned by any roadmap item. Place it, probably before #21
   or inside #37?
3. Files missing from `compare/webrtc/reference/` that this inventory needed
   (to be added through #57): `call/bitrate_allocator.cc`,
   `modules/congestion_controller/rtp/transport_feedback_adapter.*`,
   `modules/rtp_rtcp/source/rtp_sender.cc` (padding size).
