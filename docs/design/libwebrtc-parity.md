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

**Scope of this version.** Sections 3 (packet loss), 4 (jitter and timing),
5 (congestion control) and 8 (synchronization). Sections 6, 7 and the RTP session layer follow in separate
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

## 4. Jitter and timing

Reference files: `video/timing/jitter_estimator.*`,
`video/timing/frame_delay_variation_kalman_filter.*`,
`video/timing/inter_frame_delay_variation_calculator.*`,
`video/timing/rtt_filter.*`, `video/timing/timestamp_extrapolator.*`,
`video/timing/default_video_jitter_timing.*`,
`modules/video_coding/timing/timing.*`,
`modules/video_coding/timing/decode_time_percentile_filter.*`,
`modules/video_coding/packet_buffer.*`, `video/rtp_video_stream_receiver2.cc`,
`modules/rtp_rtcp/source/receive_statistics_impl.cc`,
`modules/audio_coding/neteq/*` (`delay_manager`, `underrun_optimizer`,
`reorder_optimizer`, `histogram`, `decision_logic`, `delay_constraints`,
`buffer_level_filter`, `packet_buffer`).

### 4.1 Video jitter estimate (`VideoJitterEstimator`, `src/timing/video_jitter_estimator.cc`)

The Kalman filter itself is the same model with the same constants. What
differs is everything around it: which frames feed it, how the noise and the
frame size are tracked, and how the estimate becomes a delay.

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| Kalman filter (`KalmanUpdate`) | `frame_delay_variation_kalman_filter.cc` | Constants **same**: initial slope `1/(512e3/8)`, covariance 1e-4 and 1e2, process noise 2.5e-10 and 1e-10, minimum slope `kMaxBandwidth` 1e-6, observation noise `(300 x exp(-abs(dS)/max_size) + 1) x sqrt(var_noise)` with a floor of 1. Cosmetic difference: ours divides by `max_frame_size + 1`; libwebrtc returns without updating when `max_frame_size < 1`. | same | #24 |
| Frame-size statistics (`Update`) | `jitter_estimator.cc` `UpdateEstimate`: `kPhi` 0.97, `kPsi` 0.9999, `kInitialAvgAndMaxFrameSizeBytes` 500, `kFramesUntilSizeFiltering` 5 | `phi` and `psi` **same**. Different: (1) libwebrtc starts the average and the maximum at 500 bytes and sets the average to the mean of the first 5 frames; ours sets both to the first frame's size. (2) The average accepts a frame only if `size < old_avg + 2 x std`; ours tests against the *new* average and accepts every frame for the first 30. (3) libwebrtc updates the size variance on every frame (around the new average); ours only when the average was updated. (4) libwebrtc ignores a zero-size frame; ours does not. | port | #24 |
| Which frames feed it (`RtpReceiver::AssembleFrames`, `src/rtp/rtp_receiver.cc:194`) | `default_video_jitter_timing.cc` `OnDecodableTemporalUnit`, `inter_frame_delay_variation_calculator.cc` | libwebrtc feeds the estimator when a frame becomes **decodable**, with that time, and **skips a frame that was retransmitted** (it calls `FrameNacked()` instead, which feeds the NACK term below). The calculator also drops a frame whose RTP timestamp is not newer than the previous one. Ours feeds every assembled frame with the arrival time of its last packet, retransmitted or not, and a frame with an older timestamp gives a negative RTP delta (`OnFrame`). So with reordering and repair, the delay samples include repair delay that libwebrtc leaves out. This is the first suspect for #24. | port | #24 |
| Delay clamp and outlier rule (`Update`) | `UpdateEstimate`: `kNumStdDevDelayClamp` 3.5, `kNumStdDevDelayOutlier` 15, `kNumStdDevSizeOutlier` 3 | libwebrtc **clamps** the frame delay to +-(3.5 x sigma + 0.5 ms) before the Kalman update, and treats a sample as an outlier only if the deviation is beyond 15 sigma **and** the frame is not a positive size outlier (> avg + 3 x std); an outlier still updates the noise, clipped to +-15 sigma. Ours has no clamp; an outlier (>= 15 sigma, only after 30 frames) skips the Kalman update. | port | #24 |
| Congestion rejection | `kCongestionRejectionFactor` -0.25 | libwebrtc skips the Kalman update when the frame is much smaller than the previous one (`delta_bytes <= -0.25 x max_frame_size`), because a delay increase after a size drop is queueing, not transmission time. Ours has no such rule. | port | #24 |
| Noise estimate (`var_noise_`) | `EstimateRandomJitter`, `kAlphaCountMax` 400 | `alpha_count_max` **same**. Different: libwebrtc tracks the noise **mean** and takes the variance around it; ours assumes mean 0. libwebrtc also scales `alpha` by the measured frame rate (`rate_scale` = 30 fps / fps, with a ramp-in over the first 30 samples) so the time constant is the same at other frame rates; ours has a per-frame constant. The floor of 1 ms^2 is the same. | port | #24 |
| `JitterDelay()` | `CalculateEstimate`, `GetEstimate`, `NoiseThreshold`: `kNoiseStdDevs` 2.33, `kNoiseStdDevOffset` 30 ms, `OPERATING_SYSTEM_JITTER` 10 ms, range 1 ms to 10 s | Different, and the largest constant gap: ours is `theta0 x (max - avg) + max(1, 3 x sigma - 0)`; the config comment says libwebrtc's 2.33 and 30 are "available through the config", but they are not the defaults. libwebrtc is `theta0 x (max - avg) + max(1, 2.33 x sigma - 30)`, **plus 10 ms** of OS jitter, and never lower than the value filtered after start-up (`filter_jitter_estimate_`). At 40 ms of jitter this is tens of ms of difference in the estimate before any other effect. The 10 s cap and "keep the previous estimate when below 1 ms" have no counterpart. | port | #24 |
| NACK and RTT terms | `FrameNacked`, `UpdateRtt`, `rtt_filter.cc` (`kMaxRtt` 3 s, `kFilterFactorMax` 35, `kJumpStddev` 2.5, `kDriftStdDev` 3.5), `kNackLimit` 3, `kNackCountTimeout` 60 s, `kRttMult` 0.9, `kRttMultAddCap` 200 ms | libwebrtc adds `min(0.9 x filtered RTT, 200 ms)` to the jitter delay once 3 retransmitted frames were seen within 60 s, so a stream that depends on NACK buffers one more round trip. Ours has neither the RTT filter nor the term. | port | #24 |
| Frame-rate scaling of the estimate | `GetEstimate`: `kJitterScaleLowThreshold` 5 Hz, `kJitterScaleHighThreshold` 10 Hz | libwebrtc returns 0 below 5 fps and scales linearly up to 10 fps; ours has no frame-rate dependence. Matters only for low-frame-rate streams. | port | #24 |
| Median / percentile frame-size filters | `avg_frame_size_median`, `max_frame_size_percentile`, `frame_size_window` (field trial) | Off by default in libwebrtc (`std::nullopt`), so not part of the baseline. | n/a (field trial, off by default) | |

### 4.2 Render timing (`VideoTiming`, same file)

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| Target delay (`TargetDelay`) | `VCMTiming::VideoDelayTimings::TargetDelay` = `max(min_playout_delay, minimum_delay + decode_time + render_delay)` | Same shape, and `render_delay` 10 ms is **same** (`kDefaultRenderDelay`). Different: `minimum_delay` is the jitter estimate of 4.1 (ours is larger, see above). Ours also clamps the result to a `max_playout_delay` of 10 s; libwebrtc applies the stream's playout-delay limits in `RenderTime`. | port | #24 |
| Decode time estimate (`DecodeTimeP95`) | `decode_time_percentile_filter.cc`: percentile 0.95, `kTimeLimitMs` 10000 window, `kIgnoredSampleCount` 5 | Percentile **same** (0.95). Different: ours uses the last 64 samples (`decode_time_window`) and returns 5 ms with no samples; libwebrtc uses the samples of the last 10 s and ignores the first 5. | port | #24 |
| Current delay (`UpdateCurrentDelay`) | `VCMTiming::UpdateCurrentDelay(render_time, actual_decode_time)` | Different mechanism. libwebrtc raises `current_delay` by the amount the frame was **late** relative to its render time (`delayed = decode_time - render_time + max_decode + render_delay`), capped at the target, and ignores a frame that is not late. Ours moves `current_delay` toward the target by at most 10% of wall time per second in both directions (`max_delay_change_per_s`), a smoothing that does not exist in libwebrtc. | port | #24 |
| Local capture time (`IncomingTimestamp`, `LocalCaptureTime`) | `timestamp_extrapolator.cc`: 2-state recursive least squares of local time against the RTP timestamp (`kLambda` 1, `kP11` 1e10, `kStartResidualVariance` 3000^2, `kStartUpFilterDelayInPackets` 2), updated once per complete frame (`OnCompleteFrame`) and **not for retransmitted frames** | Different algorithm. Ours takes the lower envelope of `arrival - rtp_time` with a 5 us per frame leak, and also updates on retransmitted frames. libwebrtc fits a line through all frames (so it follows a clock-rate difference, and jitter averages out rather than being excluded) and resets on a large residual (`Reset`, threshold not compared). | port | #24 |
| Low-latency rendering | `VideoDelayTimings::UseLowLatencyRendering` | If `min_playout_delay == 0` and `max_playout_delay <= 500 ms`, libwebrtc renders as soon as possible. Needs a playout-delay limit from the stream, which the harness does not send. | n/a (stream-signalled playout delay, not used in the harness) | |
| Frame scheduling after the render time | `video_stream_buffer_controller.cc` (wait for decode, force-release of keyframes), `api/video/frame_buffer.*` | Our `FrameScheduler` (`src/video/frame_scheduler.cc`, section 7) is not compared here. | not compared | #24 |

### 4.3 Video packet buffer and receive statistics

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `PacketBuffer` (`src/timing/packet_buffer.cc`) | `modules/video_coding/packet_buffer.cc` (H.264 also has `h26x_packet_buffer.cc`), `video/rtp_video_stream_receiver2.cc` `kPacketBufferStartSize` 512, `kPacketBufferMaxSize` 2048 | The window of 2048 packets matches the maximum. libwebrtc starts at 512 and doubles up to 2048 when full (at the maximum size it clears the buffer and requests a keyframe), and drops a packet more than half the buffer size behind the first sequence number; ours keeps a `std::map` window of 2048 with no growth step and drops packets 2048 behind the highest. Frame assembly: ours waits for the packet before an IDR slice that follows its parameter sets (`may_continue_frame`); libwebrtc's H.264 path handles this inside `h26x_packet_buffer.cc`, not compared in detail. | port (full-buffer behaviour and assembly rules: unplaced; compare when #23 is worked, as both feed keyframe requests) | #23 |
| `ReceiverStatistics` jitter (`src/timing/rtcp_stats.cc`) | `receive_statistics_impl.cc` `UpdateJitter`: `jitter_q4_ += ((abs(D) << 4) - jitter_q4_ + 8) >> 4` | The RFC 3550 interarrival jitter filter (1/16) is **same**. Different: libwebrtc updates only for in-order packets with a new timestamp and skips retransmitted packets, skips jumps of more than 5 s of video clock, and adjusts when the payload frequency changes (`ReviseFrequencyAndJitter`); ours keeps the value in floating point and has none of these guards. Retransmissions do reach our statistics, so they can inflate `jitter_ms` on a lossy link. | port (guards) | unplaced |
| `RttEstimator` | `rtcp_receiver.cc` report-block handling: `rtt = now - last_sr - delay_since_last_sr` | The measurement is **same** (compact NTP arithmetic). Ours keeps a 7/8 smoothed value and a minimum; how libwebrtc aggregates RTT for its consumers (`rtcp_receiver.cc`, `rtp_rtcp_impl2.cc`) was not read. The consumer in 4.1 is the `rtt_filter.cc` row above. | not compared (aggregation); filter: port | #24 |
| `ClockDriftEstimator`, `RlsLineFit`, `DriftCompensator` (`src/timing/clock_drift.cc`) | `rtc_base/rtp_to_ntp_estimator.*`, `timestamp_extrapolator.cc` | Not compared yet; belongs with synchronization (section 8) and the extrapolator row in 4.2. | not compared | #63 |

### 4.4 Audio jitter buffer (`AudioJitterBuffer`, `DelayManager`, `src/timing/audio_jitter_buffer.cc`, `delay_manager.cc`)

NetEq is a much larger module than ours: delay estimation, a packet buffer,
decision logic, a buffer-level filter and the DSP operations. The rows list
the differences found in the files above. Each belongs to #62.

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| Target level (`DelayManager::Recompute`) | `delay_manager.cc`, `underrun_optimizer.cc`, `histogram.cc` | **Same**: 0.95 quantile, forget factor 0.983 per packet (Q15 in libwebrtc). Different: (1) libwebrtc has 100 buckets of 20 ms; ours 10 ms buckets up to 2000 ms. (2) Start-up forgetting (`start_forget_weight` 2: the histogram adapts faster for the first packets) has no counterpart. (3) libwebrtc registers the **maximum relative delay in each 500 ms interval** (`resample_interval_ms` 500), not every packet's delay. (4) Initial target 80 ms (`kStartDelayMs`) against ours 60 ms. (5) Ours rounds the target up to whole packets with a floor of one packet; libwebrtc applies `DelayConstraints` (minimum and maximum delay, and a cap of 75% of the packet buffer, `delay_constraints.cc`). (6) Relative delay: libwebrtc keeps a `PacketArrivalHistory` (`kPacketHistorySizeMs` 2000) and measures against the earliest arrival in that window; ours measures against the minimum transit in a 2 s window, a similar idea with different bookkeeping. | port | #62 |
| Reorder optimizer | `reorder_optimizer.cc`, `use_reorder_optimizer` true, `reorder_forget_factor` 0.9993, `ms_per_loss_percent` 20 | Missing. libwebrtc weighs the cost of a late packet (loss) against extra delay and can pick a higher target than the underrun histogram gives. Ours uses only the quantile. | port | #62 |
| Operation choice (`MaybeStretch`) | `decision_logic.cc` `ExpectedPacketAvailable`: `low_limit` = target, `high_limit` = target + max delay in the history + `kDelayAdjustmentGranularityMs` 20, fast accelerate at 4 x `high_limit`; `kPostponeDecodingLevel` 50; `kTargetLevelWindowMs` 100; `buffer_level_filter.cc` | Different thresholds. Ours accelerates above the target (or target + one packet) and expands below 75% of it, on an exponentially filtered level (0.8/0.2). libwebrtc accelerates above `target + max arrival delay + 20 ms`, expands below the target, fast-accelerates above 4 x that, and filters the level with `BufferLevelFilter` (level factor in Q8). Ours has no fast accelerate and no `TimescaleAllowed` rule. | port | #62 |
| Concealment and merge (`plc_`, `kMerge`) | `expand.cc`, `merge.cc`, `background_noise.cc`, `normal.cc`, `comfort_noise.cc` | Different DSP. Ours uses `loss::Plc` (3.4) and a crossfade on the first good frame. libwebrtc's `Expand` builds a pitch-based extension with a background-noise fill, `Merge` re-aligns the next packet with the expanded signal by cross-correlation, and `Normal` fades the signal back in. Decay and mute ramp constants not compared. | port | #62 |
| Time stretch (`TimeStretch`, `src/timing/time_stretch.cc`) | `time_stretch.cc`, `accelerate.cc`, `preemptive_expand.cc` | Ours is a floating-point WSOLA with `correlation_threshold` 0.75 and a -55 dBFS silence gate. libwebrtc's `TimeStretch::Process` works on a downsampled signal with a peak search and a background-noise test; its thresholds are not compared constant by constant. | port | #62 |
| Packet buffer limits | `neteq/packet_buffer.cc` (`max_packets_in_buffer` from `NetEq::Config`; smart flush), `delay_constraints.cc` | Ours holds 100 packets and flushes all but 3 when over; libwebrtc has a configurable limit (the default is in `api/audio/neteq/neteq.h`, not in `reference/`, see the open questions), a smart flush tied to the target level, and discards old packets in the buffer. | port | #62 |
| Comfort noise / DTX | `comfort_noise.cc`, `decision_logic.cc` `CngOperation`, `kCngTimeoutMs` 1000 | Ours has no CNG decoding in the jitter buffer. The scenarios use continuous audio, so it is not exercised. | not compared | #62 |
| Decoder inside the buffer (Opus PLC, in-band FEC) | `neteq_impl.cc` decode loop, `audio_decoder_opus.cc` | Ours receives decoded PCM, not packets; there is no decoder in the buffer. Decoding (and Opus in-band FEC, decoder PLC) is a test bench model. See also 3.3 and 3.4. | port (model) | #62 |

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

## 8. Synchronization

Reference files: `video/stream_synchronization.*`, `video/rtp_streams_synchronizer2.*`,
`rtc_base/rtp_to_ntp_estimator.*`, `modules/rtp_rtcp/source/remote_ntp_time_estimator.cc`,
`modules/rtp_rtcp/source/ntp_time_util.*`, `modules/rtp_rtcp/source/rtp_sender_video.cc`
(abs-capture-time on send), `video/rtp_video_stream_receiver2.cc` (abs-capture-time on
receive, `GetSyncInfo`), `audio/audio_send_stream.cc` (extension registration),
`media/engine/webrtc_video_engine.cc` (base minimum playout delay).

**Where it runs.** The section 8 modules are used by their unit, e2e and bench
tests only. The test bench call (`testbench/call.cc`) registers the transport-cc
extension and nothing else from section 8: it does not map RTP to NTP, does not
run `StreamSynchronization` and sends no abs-capture-time. The comparison harness
(`compare/webrtc/webrtc_call.cc`) registers the same one extension and sets no
sync group, so **neither side synchronizes audio and video in any `sm_compare`
scenario today**. Section 8 parity is therefore not measured; #63 owns the metric
(`sync.av_offset_p95_ms`), and wiring the modules into the session layer is
unplaced (see the last table).

### 8.1 RTP timestamp to NTP (`RtpToNtpEstimator`, `src/sync/rtp_to_ntp.cc`)

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| Regression and window | `rtc_base/rtp_to_ntp_estimator.cc` (`UpdateParameters`, `kNumRtcpReportsToUse` 20) | **Same**: least squares of NTP on the unwrapped RTP timestamp over the last 20 SRs, slope gives the media clock rate (`EstimatedFrequencyKhz`). Ours works in milliseconds as `double`; libwebrtc in NTP units (2^-32 s) as `double`, and rounds the estimate with `+ 0.5`. The difference is far below a microsecond. libwebrtc computes the line from 2 points; ours also requires `slope > 0` before it is valid. | same | #63 |
| Duplicate measurement | `UpdateMeasurements` (`kSameMeasurement`) | **Same** rule (equal NTP or equal unwrapped RTP). Ours tests `py == y` after converting to milliseconds, so two NTP values less than a microsecond apart count as the same; libwebrtc compares exact NTP values. | same | #63 |
| Rejecting a bad SR | `UpdateMeasurements` (`invalid_sample`, `kMaxInvalidSamples` 3, `kMaxAllowedRtcpNtpInterval` 1 h, RTP jump limit `1 << 25`) | Different rule. libwebrtc compares the new SR with the **newest** stored one: NTP must move forward and by less than 1 h, RTP must move forward and by less than `1 << 25` ticks; after 3 consecutive invalid samples it clears the history. Ours predicts the new point from the **fitted line** and rejects a point more than `max_deviation` **30 ms** off (once 3 points exist); after 3 consecutive rejections it resets. Ours therefore rejects SRs delayed in the sender by more than 30 ms, which libwebrtc accepts, and does not check direction or the 1 h and `1 << 25` limits. Also: libwebrtc keeps the history sorted newest first and a new measurement older than the front in RTP is invalid (ours unwraps with `SeqNumUnwrapper` and would accept it if it fits the line). | port | unplaced |
| `Estimate` of a timestamp | `Estimate` | libwebrtc unwraps with the same unwrapper that `UpdateMeasurements` advances (`unwrapper_.Unwrap`); ours uses `PeekUnwrap`, which does not move it. Equal for normal use. | same | |

### 8.2 Audio/video synchronization (`StreamSynchronization`, `src/sync/stream_sync.cc`)

libwebrtc splits the work between `RtpStreamsSynchronizer` (`rtp_streams_synchronizer2.cc`,
what to measure and when) and `StreamSynchronization` (`stream_synchronization.cc`, the
filter and the delay rule). Ours is one class and the caller decides when to call `Update`.

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| Constants | `kMaxChangeMs` 80, `kMaxDeltaDelayMs` 10000, `kFilterLength` 4, `kMinDeltaMs` 30 | **Same** values: tolerance 30 ms, step limit 80 ms, filter weight 1/4 (`filter = 0.25`), maximum extra delay 10 s. | same | |
| Relative delay | `ComputeRelativeDelay` | **Same** formula: `(video_receive - audio_receive) - (video_capture - audio_capture)`, capture times from the two RTP-to-NTP maps. Different: libwebrtc returns "no update" when the result is beyond +-10000 ms; ours has no such check. The receive times are those of the **latest received RTP packet** of each stream (`GetSyncInfo`: `last_received_rtp_system_time_`, the matching RTP timestamp), where ours is given the arrival of the last audio packet and the last video **frame** (`OnVideoFrame`). | port | unplaced |
| Offset filter | `ComputeDelays` (`avg_diff_ms_`, integer arithmetic) | Different: (1) libwebrtc starts the average at **0** and filters from the first sample (`(3 x avg + cur) / 4`), so the first update sees one quarter of the offset; ours starts at the first sample. (2) After a correction libwebrtc **resets the average to 0**; ours subtracts the applied step, keeping the remainder. (3) libwebrtc uses integer milliseconds (truncating division); ours uses `double`. (4) The measured input differs in what it contains: libwebrtc's `current_diff = current_video_delay - current_audio_delay + relative_delay`, where `current_video_delay` is the video target delay passed in (the previous output when it applied one), and the audio delay is the current audio delay; ours adds the *extra delays it applied itself* to the caller's delays. Whether the two inputs are the same quantity depends on what `VideoReceiveStream2` reports as `current_delay`, and that file is not in `reference/`. | port | unplaced |
| Delay rule | `ComputeDelays` (`audio_delay_`/`video_delay_` with `extra_ms` and `last_ms`) | Different: libwebrtc changes **one stream at a time**. Video late: lower the video extra delay if it is above the base target, otherwise raise the audio extra delay, and set the other stream's extra delay back to the base target. Video early: lower the audio extra delay if any, otherwise raise the video's. The output is `extra_ms` for the stream that moved and **`last_ms`** (the previous output) for the other, each capped at base + 10 s and with video never below base. Ours moves video then audio in one step (`from_video`, remainder to audio), so both extra delays can change in one update, and clamps each extra delay to 0..10 s. | port | unplaced |
| What the output means | `rtp_streams_synchronizer2.cc` `UpdateDelay` | libwebrtc passes the result to `SetMinimumPlayoutDelay(target_ms)` of the audio and video streams: a **minimum total playout delay** (audio `target_audio_delay_ms` starts at 0, video at its current delay). Ours returns `SyncDelays` as **extra delay to add** to the current delay. The two are equal only when the caller adds the extra delay to the current delay and re-reads it; the library has no caller that does either (see "Where it runs"). | port | unplaced |
| When it runs | `kSyncInterval` 1 s repeating task; skipped when no new audio packet **and** no new video packet arrived since the last run, when either stream has no SR yet, or when `RtpToNtpEstimator` is not valid | Ours has no schedule and no "new packet" rule; the caller chooses. It returns nothing until both streams have an estimate. | port | unplaced |
| Failed delay change | `ReduceAudioDelay`, `ReduceVideoDelay` (extra delay x 0.9 when `SetMinimumPlayoutDelay` returns false) | No counterpart. | port | unplaced |
| Base target delay | `SetTargetBufferingDelay`, `base_target_delay_ms_`; the engine's `SetBaseMinimumPlayoutDelayMs` (`webrtc_video_engine.cc`) | No counterpart: ours has no base delay that both streams are lifted to. | port | unplaced |
| Which streams are paired | `RtpStreamsSynchronizer::ConfigureSync(Syncable* audio)` called per **sync group** by `Call` (`call/call.cc`, not in `reference/`) | `MediaSyncGroups` pairs by **CNAME** and takes the lowest SSRC of each kind. libwebrtc pairs by the `sync_group` string of the receive stream configuration (the stream id from SDP), one audio stream per video stream. Same purpose, different key; `call/call.cc` and `call/syncable.h` are needed to confirm. | file needed (#57) | unplaced |
| Stream offset stat | `GetStreamSyncOffsetInMs` (video playout NTP vs audio playout NTP, estimated frequency) | No counterpart: ours reports `av_offset_ms` computed inside `Update`. This is the quantity #63's metric needs. | port | #63 |

### 8.3 Capture time and glass-to-glass latency (`src/sync/capture_time.cc`)

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| `AbsoluteCaptureTimeSender` | `absolute_capture_time_sender.*`, called from `rtp_sender_video.cc` line ~695 | libwebrtc builds the extension for **video** from the frame's capture time when the caller gave none (NTP of the capture time, estimated clock offset 0) and puts it on the **first packet of a frame only**, and only when its `OnSendPacket` returns a value. The sender's constants (resend interval, interpolation error limit) are in `absolute_capture_time_sender.cc`, which is not in `reference/`, so ours (1 s, 1 ms) is not compared. Different in structure: ours decides per packet; libwebrtc per frame (first packet). The audio send path only registers the extension (`audio_send_stream.cc`). | file needed (#57) | unplaced |
| `AbsoluteCaptureTimeInterpolator` | `absolute_capture_time_interpolator.*`, called from `rtp_video_stream_receiver2.cc` line ~781 (video clock rate 90 kHz) | The interpolation rule and its time limit are in a file that is not in `reference/` (ours: 5 s). libwebrtc also adjusts the received offset with a `CaptureClockOffsetUpdater` (`capture_clock_offset_updater`, file needed); ours has no counterpart. | file needed (#57) | unplaced |
| Wire format of the extension | WebRTC experimental extension `http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time`, 8 or 16 bytes | **Same** layout (UQ32.32 capture time, optional Q32.32 offset). Interop with libwebrtc's parser has not been run. | same (interop check unplaced) | unplaced |
| `LatencyEstimator` (SR clock offset) | `RemoteNtpTimeEstimator` (`UpdateRtcpTimestamp`, `EstimateRemoteToLocalClockOffset`) | Same input: SR NTP time, arrival NTP time and RTT/2 as the delivery time. Different filter: libwebrtc keeps the last `kClocksOffsetSmoothingWindow` **7** offsets and uses their **median** (`MovingMedianFilter`), and reports an offset only after `kMinimumNumberOfSamples` **3**; ours takes the **minimum** and leaks 5% of the gap upward per SR, with no minimum count. Ours is biased low by design (least-queued SR), the median is not. | port | unplaced |
| Where capture time becomes a latency | `rtp_video_stream_receiver2.cc` line ~1037: only when the field trial `WebRTC-UseAbsCapTimeForG2gMetric` is enabled (not default; it is a retiring trial in `experiments/field_trials.py`) is the absolute capture time (plus the local clock offset) used, for the glass-to-glass (g2g) metric. | `LatencyEstimator::OnRendered` exposes the latency as a library metric (`capture_to_render_ms`). libwebrtc does not report it by default, so there is no libwebrtc value to compare with in `sm_compare`. | n/a (library extra; deviation to be decided) | unplaced |

### 8.4 Header extensions and BUNDLE demultiplexing (`src/sync/header_extensions.cc`)

No libwebrtc source for this area is in `reference/`: the RFC 8285 parser and writer
(`rtp_packet.cc`, `rtp_header_extensions.cc`), `RtpHeaderExtensionMap` and the
BUNDLE demuxer (`call/rtp_demuxer.cc`, `call/rtp_stream_receiver_controller.*`) are
missing. The table records what can be said without them.

| Our module | libwebrtc | Difference | Status | Issue |
|---|---|---|---|---|
| One- and two-byte extension forms, `ExtensionMap` | `rtp_packet.cc`, `rtp_header_extension_map.cc` | Not compared (file needed). Our `WriteHeaderExtensions` picks the one-byte form when all elements fit. Interop with libwebrtc's parser is covered for transport-cc and MID in `compare/webrtc/rtcp_interop.cc`; other extensions are not. | file needed (#57) | unplaced |
| `RtpDemuxer` order (MID(+RID), known SSRC, unique payload type) | `call/rtp_demuxer.cc` (`ResolveSink`: SSRC sinks, then MID + RSID, then payload type) | Not compared (file needed). The one-to-one product decision keeps BUNDLE demultiplexing (`program.md` 0.1), but simulcast RID routing is only needed by an SFU. | file needed (#57) | unplaced |
| Extensions the harness registers | `webrtc_call.cc` | Only transport-wide sequence number. MID, abs-send-time and abs-capture-time are never on the wire in a comparison scenario. | n/a | |

### 8.5 Items for the Owner to place

Rows marked `unplaced` above fall into three groups; none has an issue yet:

1. **Port `StreamSynchronization` and `RtpStreamsSynchronizer` as libwebrtc has
   them** (8.1 rejection rule, 8.2 filter, delay rule, schedule, `ReduceDelay`,
   base delay). Needs a place on the work list after #63 builds the metric, since
   the library does not use the module today.
2. **Wire sync into the session layer** (`src/rtp`): SR to `RtpToNtpEstimator`,
   `GetSyncInfo`-like inputs, `SetMinimumPlayoutDelay` on the audio jitter buffer
   and video timing. Without it no scenario can show a sync gap.
3. **Add to #57:** `modules/rtp_rtcp/source/absolute_capture_time_sender.*`,
   `absolute_capture_time_interpolator.*`, `capture_clock_offset_updater.*`,
   `call/call.cc` (`ConfigureSync`), `call/syncable.h`,
   `video/video_receive_stream2.cc` (`GetInfo`, `current_delay`),
   `call/rtp_demuxer.cc`, `modules/rtp_rtcp/source/rtp_packet.cc`,
   `rtp_header_extensions.cc`, `rtp_header_extension_map.cc`.

## Not yet inventoried

These areas will be added in separate PRs for #59, in this order. Their
tables go in this file.

- 6 Audio quality
- 7 Video quality
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
4. For section 4: `api/audio/neteq/neteq.h` (the default `max_packets_in_buffer`
   and the other `NetEq::Config` defaults), `modules/audio_coding/neteq/packet_arrival_history.*`
   is present, but `relative_arrival_delay_tracker.*` (named in #62) is not in
   `reference/` and the 4.4 row on relative delay is read from
   `packet_arrival_history.cc` and `delay_manager.cc` only. Add them through #57.
5. For section 8: the files listed in 8.5 item 3 (abs-capture-time sender and
   interpolator, `call/call.cc`, `video_receive_stream2.cc`, the demuxer and the
   RTP packet parser) are missing; the rows that need them say "file needed".
   Should the library wire A/V sync into the session layer before #63's metric
   exists (8.5 items 1 and 2)?
