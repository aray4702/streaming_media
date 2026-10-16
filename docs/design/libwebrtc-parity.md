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

**Scope of this version.** Sections 3 (packet loss) and 5 (congestion
control). Sections 4, 6, 7, 8 and the RTP session layer follow in separate
PRs; their headings are below with the state `not yet inventoried`.
Rows were found by reading our code next to libwebrtc's. A constant listed as
`same` was compared; a module not listed has not been compared yet. No code
is changed by this document.

## 3. Packet loss

Reference files: `modules/video_coding/nack_requester.*`,
`modules/rtp_rtcp/source/rtp_packet_history.*`, `rtp_sender_egress.cc`,
`ulpfec_generator.*`, `ulpfec_receiver.*`, `forward_error_correction*.{h,cc}`,
`fec_private_tables_*.h`, `modules/video_coding/media_opt_util.*`,
`fec_controller_default.*`, `fec_rate_table.h`, `loss_notification_controller.*`,
`video/rtp_video_stream_receiver2.cc`, `call/rtp_video_sender.cc`.

### 3.1 NACK and RTX

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `NackTracker` gap detection and list limits | `video_coding/nack_requester.cc` (`kMaxPacketAge` 10000, `kMaxNackPackets` 1000, `kDefaultRtt` 100 ms, `AddPacketsToNack`) | Constants **same**: 10000, 1000, initial RTT 100 ms, a NACK for every gap at the packet that reveals it (libwebrtc's reordering wait is `WaitNumberOfPackets(0.5)`, which is 0 because the reordering histogram is never updated: `is_retransmitted` is hard-coded `true`). Different: (1) on a full list libwebrtc clears the whole list and requests a keyframe; ours first drops the gaps older than the next keyframe and keeps the rest (`RecoverKeyframeSpace`), and tracks keyframe positions for that. (2) A packet recovered by FEC or RTX **before** the gap is seen is never added to the list (`recovered_list_`); ours learns about it through the normal receive path and may NACK a packet that is already recovered. (3) `ClearUpTo(seq)` (drops list entries before a given sequence number): no counterpart found in ours, which only ages entries out. | port | #23 |
| `NackTracker::GetNackBatch` retry and give-up | `nack_requester.cc` (`GetNackBatch`, `kMaxNackRetries` 100, `kUpdateInterval` 20 ms) | Retry spacing is the same rule (`now - sent_at >= rtt`); ours has a 20 ms floor (`min_retry_interval`) that libwebrtc does not have, and both resend at the RTT. Different: ours gives up after **10** retries and asks for a keyframe; libwebrtc gives up after **100** retries and does **not** request a keyframe (the entry is only erased with a log line). When libwebrtc sends the batch also differs: on the packet that reveals the gap (`kSeqNumOnly`, buffered with other feedback) and every 20 ms for retries (`kTimeOnly`); when ours sends is the `RtpReceiver::Process` cadence, which has not been compared. | port | #23 |
| `RtxSender` history | `rtp_rtcp/source/rtp_packet_history.*`, `rtp_sender_egress.cc` | Different: (1) libwebrtc stores packets while it is allowed to retransmit, with a minimum retention of `max(3 x RTT, 1 s)` (`kMinPacketDurationRtt`, `kMinPacketDuration`), culls after three times that and at most 9600 packets (`kMaxCapacity`); ours is a fixed ring of 1024 packets and refuses packets older than 2 s. In the harness libwebrtc is configured with `nack.rtp_history_ms = 1000`. (2) Ours stores the packet when it is queued for the pacer (`RtpSender::StoreAndQueueMedia`), so a NACK can ask for a packet that has not left yet. libwebrtc also stores at queueing, but marks such a packet not yet sendable and refuses a retransmission before `send_time + rtt_multiplier x RTT` (`earliest_allowed_retransmission`, `retransmit_flushed_packets_settings_`); ours has no such rule. (3) Request throttling: libwebrtc ignores a repeat request while `now - send_time < RTT` (the send time moves forward on each retransmission, `VerifyRtt`); ours keeps a separate `last_resent` and the factor `min_resend_rtt_factor` 1.0, which is equivalent in effect. (4) libwebrtc can send stored packets as **payload padding** (`GetPayloadPaddingPacket`, `kMaxPaddingHistory` 63) when probing or padding; ours pads with a fixed dummy packet (see 5.5, padding). | port | #21 (budget), #70 (padding); history size and culling: unplaced |
| RTX encapsulation (`MakeRtxPacket`, `RestoreFromRtx`) | `rtp_rtcp/source/rtp_sender.cc` (not in `reference/`), RFC 4588 | Wire format **same** as RFC 4588 (original sequence number prepended to the payload, separate SSRC and payload type). libwebrtc also sends RED packets through RTX (`red_rtx_payload_type`, `kRtxRedPt` in the harness); ours has no RED on video. Padding on the RTX SSRC takes its numbers from the same counter in both. | same (RED-over-RTX: with #29) | #29 |
| Retransmission rate cap | `RtpSenderEgress` / `retransmission_rate_limiter` (none configured in M157) | Ported on PR #55 (Owner decision: no cap, measured repair share capped at 50% as `FecControllerDefault`). Open until PR #55 merges. | port (in review) | #21 |

### 3.2 FEC

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `FecEncoder` / `FecDecoder` wire format (`loss/fec.cc`) | `rtp_rtcp/source/ulpfec_header_reader_writer.*`, `ulpfec_generator.*`, `ulpfec_receiver.*`, RFC 5109 | **Custom format**, not ULPFEC: `base_seq:16, mask:64, len_xor, ts_xor, pt_marker_xor` on a **separate FEC SSRC** and payload type. libwebrtc sends ULPFEC inside RED on the media SSRC (RED payload type on every media packet while FEC is on, `kRedForFecHeaderLength` 1), with the RFC 5109 FEC header (E and L bits, 16- or 48-bit masks, protection length), and recovers through `ForwardErrorCorrection`. The product decision (`program.md` 0.1) is ULPFEC over RED as the only FEC format. | port (format only; FEC is off beside NACK, see the last row) | #29 |
| Packet masks | `forward_error_correction_internal.*`, `fec_private_tables_random.h`, `fec_private_tables_bursty.h`, `kUlpfecMaxMediaPackets` 48 | Ours builds a mask from the loss pattern: `kInterleaved` (parity `j` covers packets `i % m == j`) and `kBlock`, chosen from the mean burst length. libwebrtc uses the fixed mask tables by (media packets, FEC packets), **random** by default (`FecControllerDefault` always passes `kFecMaskRandom`), and bursty only when the caller asks. The group size limit of 48 is the same. | port (format only; FEC is off beside NACK, see the last row) | #29 |
| Grouping rule (`FecEncoder::AddMediaPacket`, one frame per group, `max_group_size` 48) | `UlpfecGenerator::AddRtpPacketAndGenerateFec`, `max_fec_frames`, `kMinMediaPackets` 4, `kHighProtectionThreshold` 80 (of 255), `kMaxExcessOverhead` 50 (Q8), `kMinMediaPacketsAdaptationThreshold` 2.0 | Ours protects one frame at a time. libwebrtc spans up to `max_fec_frames` frames (computed from RTT, see 3.7), or emits earlier once the excess overhead is below `kMaxExcessOverhead` and `kMinMediaPackets` packets (when the rate is above `kHighProtectionThreshold`) have been collected. FEC packets for a keyframe use their own rate (`key_params`). | port (format only; FEC is off beside NACK, see the last row) | #29 |
| FEC packet count | `ForwardErrorCorrection::NumFecPackets(n, fec_rate)` (Q8 rate, rounding) | Ours: `FecPacketsFor(k, protection)` = `ceil(k x protection)`, at least 1. libwebrtc: `(n x rate_q8 + 128) >> 8` (round to nearest), at least 1 when the rate is above 0. Ours rounds up, libwebrtc to nearest, so ours sends more FEC packets for the same rate. | port (format only; FEC is off beside NACK, see the last row) | #29 |
| Receiver (`FecDecoder::TryRecover`) | `ulpfec_receiver.cc`, `ForwardErrorCorrection::DecodeFec` | Both are iterative XOR recovery; libwebrtc also drops FEC packets older than `kOldSequenceThreshold` 0x3fff and handles RED-wrapped media in the same path. Details of the recovery order not compared. | port (format only; FEC is off beside NACK, see the last row) | #29 |
| **Whether FEC runs at all next to NACK** | `call/rtp_video_sender.cc` `ShouldDisableRedAndUlpfec` | libwebrtc turns RED and ULPFEC **off** when NACK is on unless the codec is VP8 or VP9 (or `WebRTC-GenericPictureId`): "NACK+ULPFEC is a waste of bandwidth since ULPFEC packets also have to be retransmitted". This library's video is H.264, and the comparison harness (`compare/webrtc/webrtc_call.cc`) configures `payload_name = "Generic"` with NACK and ULPFEC both set, so by this rule **libwebrtc runs the harness scenarios without FEC**. That fits the repair overhead of 0.1-8.7% on the loss scenarios in #64, against 30-57% here, but it was read from the code and not confirmed by a run. | port (Owner decision 2026-10-16: duplicate the rule, so no video FEC while NACK is on for H.264 and Generic) | #7 |

### 3.3 RED audio

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `RedEncoder`, `RedReceiver`, `ParseRed` (RFC 2198, distance 1) | `modules/audio_coding/codecs/red/audio_encoder_copy_red.*` and the NetEq RED splitter (not in `reference/`) | Libwebrtc's audio RED (payload type 63 in the harness) repeats the previous Opus frame once. Ours is distance 1 by default (`RedEncoder(distance)`), with a 1023-byte cap on redundant blocks. Wire format RFC 2198 in both. The libwebrtc settings (when redundancy is dropped at a bitrate, the 1023-byte limit) cannot be checked without the file. | cannot compare: file needed | list on #57 |
| Opus in-band FEC use | `modules/audio_coding/codecs/opus/audio_encoder_opus.cc` (not in `reference/`) | The harness asks libwebrtc for `useinbandfec=1`; the codec is not in this library (the bench sends modelled payloads). | cannot compare: file needed | #62 |

### 3.4 Packet loss concealment

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `PacketLossConcealer` (pitch repeat, fade, merge) | NetEq `Expand`, `Merge`, `Normal`, `Accelerate`, `PreemptiveExpand` (`modules/audio_coding/neteq/`, not in `reference/` here; PR #68 adds the audio sources) | A pitch-repeat concealer with its own fade (10 ms, then 50 ms linear) and a 5 ms merge. NetEq's expand uses LPC-based synthesis with a voicing-dependent mute factor and its own cross-fade rules. A different algorithm for the same job. | port | #62 |

### 3.5 Keyframe requests and loss notification

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `KeyframeRequester` triggers | `video/rtp_video_stream_receiver2.cc` (`RequestKeyFrame`, `OnAssembledFrame`, `RtcpFeedbackBuffer`), `video_coding/nack_requester.cc` | libwebrtc requests a keyframe only when: the first assembled frame is not a keyframe; the packet buffer is cleared (overflow of 2048 packets, `kPacketBufferMaxSize`); the NACK list overflows; a sequence-number discontinuity is waiting for a keyframe (re-requested every `max_wait_for_keyframe`); the video structure is missing; the H.264 parameter sets are missing; and on a decode failure (in `video_receive_stream2.cc`, not in `reference/`). Ours also requests (a) after a frame has been blocked longer than `max(200 ms, 2 x RTT)` (the repair window), (b) after 10 NACK retries (3.1), (c) a retry when no keyframe comes within `2 x max(100 ms, 1.5 x RTT)`. These extra triggers are the source of the 11-78 keyframe requests per call in #23 (libwebrtc 0-1). | port | #23 |
| Request throttling | `rtcp_receiver.cc` `kRtcpMinFrameLength` 17 ms (the **sender** ignores a PLI or FIR within 17 ms of the last one); `RtpVideoStreamReceiver2` has no receiver-side throttle except `max_wait_for_keyframe` for discontinuities | Ours throttles at the receiver (`max(100 ms, 1.5 x RTT)`), and no 17 ms guard on the sending side was found in `src/rtp`. | port | #23 |
| Loss notification (`send_lntf`, `FeedbackType::kLntf`) | `video_coding/loss_notification_controller.*` | libwebrtc sends LNTF only when the stream has a generic descriptor with frame ids and dependencies (`LossNotificationController::OnAssembledFrame`); H.264 has none, so in the harness libwebrtc sends PLI and NACK only. The bench already disables LNTF for this reason (`testbench/call.cc`). The wire format is the same (interop-checked). The controller's own algorithm is not ported. | n/a for H.264 (revisit if a codec with a generic descriptor is added) | #23 (rename of `issue_18_lntf_not_pli`) |
| FIR | `rtcp_receiver.cc` `HandleFir` (request sequence number check), `rtp_rtcp_impl2.cc` | Wire format same. libwebrtc sends the one method set in `rtp.keyframe_method` (PLI or FIR, `RtpVideoStreamReceiver2::RequestKeyFrame`); ours sends FIR at stream start (`OnStreamStart`) and PLI afterwards. The default method of the harness stream was not checked. | port | #23 |

### 3.6 Temporal layers

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `TemporalLayerScheduler` | `modules/video_coding/codecs/vp8/default_temporal_layers.*`, `svc/` (not in `reference/`); `video/video_stream_encoder.cc` does not decide layers | libwebrtc's temporal layer patterns belong to the VP8/VP9/AV1 encoders. This library does not run codecs (`program.md` 0.1); H.264 as used here has no temporal layers in the RTP path. | n/a (codec's job) | none |
| `TemporalLayerFilter` (forwarding a subset of layers) | none in the sender/receiver; the SFU does this | Only an SFU or multiparty call needs it. `program.md` 0.1: such components are removed unless the Owner changes it. | n/a (SFU-only; candidate for removal on the Owner's say) | none |

### 3.7 Protection selection

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `ProtectionController::Update` | `video_coding/fec_controller_default.cc` (`UpdateFecRates`), `media_opt_util.cc` (`VCMNackFecMethod`, `VCMFecMethod`, `VCMLossProtectionLogic`) | A different design. Ours picks the smallest parity count `m` with `P(X > m)` below a residual target from the binomial distribution, with a target between 10% and 1% by RTT (NACK-only at `<= 30 ms`, FEC-only at `>= 150 ms`), loss smoothed by an EMA of 0.3, mask by burst length, overhead cap 50%. libwebrtc: the method is fixed by what is enabled (`kNackFec` when both are on), the loss is a **max filter over 10 s** (`kLossPrHistorySize` 10 x `kLossPrShortFilterWinMs` 1000 ms; `kMaxFilter`), the FEC rate comes from the table `kFecRateTable` indexed by the bitrate per frame and the loss in 1/255 (`fec_rate_table.h`, `kPacketLossMax` 129), with a separate rate for key frames (`RequiredProtectionFactorK` and `D`) and a resolution factor; for delta frames the rate is set to **0 when the RTT is below 20 ms** (`kLowRttNackMs`), and left unchanged at higher RTT (the RTT adjustment is the constant 1.0; the high threshold is -1, so NACK is never switched off). FEC is turned off altogether when the bytes per frame are below 400 / 700 / 1000 (by resolution) with RTT < 200 ms (`BitRateTooLowForFec`). `max_fec_frames` = `max(2 x fps_base x RTT + 0.5, 1)` capped at 6 (`kUpperLimitFramesFec`), 1 for more than two temporal layers. Mask type is always random. | port | #7 |
| Source rate after protection | `FecControllerDefault::UpdateFecRates` (overhead of NACK + FEC measured over the last second, capped at `kProtectionOverheadRateThreshold` 0.5) | Ported in PR #55 (measured share capped at 0.5). | port (in review) | #21 |
| Input to the controller (`ProtectionInput`: loss, RTT, burst length, packets per frame) | `UpdateWithEncodedData` (packets per frame from the encoded size and the max payload, exponentially filtered with 0.9999 at start, separate for key frames), `SetEncodingData` (size, layers, payload size 1460 by default) | Ours takes packets per frame from the last frame; libwebrtc filters it per frame type with `ExpFilter`. The burst length input has no libwebrtc counterpart. | port | #7 |

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
| `Pacer` | `modules/pacing/pacing_controller.cc`, `prioritized_packet_queue.cc`, `interval_budget.cc` | Different mechanism, same intent. Ours: a media-debt leaky bucket with a 5 ms burst allowance and a drain boost so no packet waits more than `max_queue_time` 2 s. libwebrtc: `PacingController` with a `send_burst_interval` of 40 ms by default (`PacerConfig::kDefaultTimeInterval`), `media_debt_` capped at `kMaxDebtInTime` 500 ms, `kMaxElapsedTime` 2 s, `kCongestedPacketInterval` 500 ms, padding at the padding rate with `kTargetPaddingDuration` 5 ms, and a large-queue drain (`MaybeUpdateMediaRateDueToLongQueue`, limit `kMaxExpectedQueueLength` 2 s, based on the **average** queue time, not the oldest packet). Priorities: libwebrtc 0 audio, 1 audio retransmission, 2 video retransmission, 3 video **and FEC together**, 4 padding; ours 0 audio, 1 retransmission, 2 video, 3 FEC, 4 padding, plus audio bypass of the budget. | port | #70 |
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

- 4 Jitter and timing (packet buffer, RTCP statistics, delay manager, time
  stretch, audio jitter buffer, video jitter estimator, render timing)
- 6 Audio quality
- 7 Video quality
- 8 Synchronization
- RTP session layer (`src/rtp`)

## Open questions for the Owner

1. `OutageMonitor` (5.4): keep as a deviation, or port libwebrtc's
   no-feedback rule and drop it?
2. ~~FEC next to NACK (3.2)~~ Decided by the Owner on PR #71 (2026-10-16):
   `ShouldDisableRedAndUlpfec` disables RED and ULPFEC for H.264 or Generic
   with NACK, and the harness uses Generic + NACK, so libwebrtc runs the loss
   scenarios without video FEC. #7 is re-scoped to duplicate that rule and
   moves to Phase 1 after #21; #29 (ULPFEC format) moves to the end of stage 1.
3. Files missing from `compare/webrtc/reference/` that this inventory needed
   (to be added through #57): `call/bitrate_allocator.cc`,
   `modules/congestion_controller/rtp/transport_feedback_adapter.*`,
   `modules/rtp_rtcp/source/rtp_sender.cc` (padding size).
   For section 3: `modules/audio_coding/codecs/red/`, the Opus encoder
   wrapper and `modules/audio_coding/neteq/` are in PR #68; the rows marked
   "file needed" for them are re-checked once it merges.
