"""Catalog of the algorithms implemented for sections 3-8 of
docs/02-webrtc-problems-solved.md. Used by tools/gen_docs.py.

Each entry maps an algorithm to the problem rows it addresses, its code,
its test suites and benchmarks, plus hand-written notes: the end-to-end
scenario, design notes that go beyond the header comment, and what
verification found (defects fixed, limitations, deviations from libwebrtc).
"""

SECTIONS = {
    "3": ("Packet loss", "loss"),
    "4": ("Jitter, reordering and timing", "timing"),
    "5": ("Bandwidth, congestion and unstable links", "cc"),
    "6": ("Audio quality (APM)", "audio"),
    "7": ("Video quality and processing", "video"),
    "8": ("Synchronization", "sync"),
    "9": ("RTP/RTCP session layer", "rtp"),
}

ALGORITHMS = [
    # ------------------------------------------------------------------ 3
    dict(id="3.1", slug="nack-rtx", title="NACK + RTX retransmission",
         problems=["Lost video packets -> NACK + RTX: selective retransmission on a separate SSRC"],
         headers=["loss/nack_tracker.h", "loss/rtx.h", "loss/rtcp_feedback.h"], unit=["NackTracker", "Rtx", "RtcpFeedback"], e2e=["E2E_NackRtx"],
         bench=["loss/NackTracker_PacketWith5PctLoss", "loss/RtxSender_StoreAndResend", "loss/Q_NackRtx_5pct_rtt80"],
         scenario="30 fps video (8 x 1000 B packets per frame) over a link with 5% random loss in both directions and "
                  "40 ms one-way delay. The receiver's NackTracker sends NACKs over the lossy reverse link, the "
                  "RtxSender answers with RTX packets. A packet counts as on time if it is available within a 250 ms "
                  "playout budget. A second run at 150 ms one-way delay shows retransmissions arriving too late.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "Retry spacing is max(RTT x factor, 20 ms) so a request is not repeated before its answer could "
               "arrive; the sender also refuses to resend the same packet twice within one RTT, which protects "
               "against duplicate NACKs. Overflow handling drops gaps older than the next keyframe instead of "
               "the whole list, so a burst does not force an unnecessary keyframe. The request is the standard generic "
               "NACK (RFC 4585 6.2.1, PID+BLP items) from loss/rtcp_feedback.h, byte-compatible with libwebrtc's "
               "rtcp_packet::Nack (interop check in compare/webrtc/rtcp_interop.cc). "
               "RTX history version 2 (`rtx v2`, issue #76 group 2) is libwebrtc's RtpPacketHistory: a packet enters when "
               "the pacer sends it, stays max(3 x RTT, 1 s) and is culled after three times that (or when 600 are held "
               "and the oldest is past the retention, at most 9600), leaves when transport feedback acknowledges it, and a "
               "request is ignored while a resend is queued or within one RTT of the last send. A queued resend that a keyframe flush discards stays pending, so later requests for it are ignored, as libwebrtc's flush leaves its history entry (issue #151). With `rtx v2` and no FEC the RTP sequence number is "
               "assigned when the pacer releases the packet (`RtpSender::AssignSequenceNumber`, as `PacketRouter` / `AssignSequenceNumber` do), so a packet a "
               "keyframe flush discards leaves no gap (issues #127, #154, [fix record](../../fixes/154-sequence-at-pacer-release.md)). Remaining differences: "
               "no early resend of packets that transport-wide feedback reports lost (RtpVideoSender::OnPacketFeedbackVector), "
               "and retransmitted-flush settings are off as in the libwebrtc default. With `pacer v2` the history also gives payload "
               "padding (`GetPayloadPaddingPacket` in `PaddingMode::kRecentLargePacket`: the most recent large payload, "
               "kept after it has left the history), see docs/fixes/120-rtx-payload-padding.md. Version 2 of the NackTracker retries on libwebrtc's 20 ms process tick.",
         verification="No defects found. Measured overhead (~5.4%) tracks the loss rate, as expected for "
                      "selective retransmission. `issue_151_outage_3s_v2` holds the freeze total at < 3300 ms "
                      "(libwebrtc 3221 ms on seed 1) since #151 turned the outage monitor off under `cc v2`."),
    dict(id="3.2", slug="fec", title="Forward error correction (XOR, ULPFEC/FlexFEC style)",
         problems=["Loss where a retransmit would arrive too late (high RTT) -> FEC: ULPFEC, FlexFEC"],
         headers=["loss/fec.h"], unit=["Fec"], e2e=["E2E_Fec"],
         bench=["loss/FecEncode_10pkt_frame_50pct", "loss/FecDecode_RecoverOne", "loss/Q_Fec_5pct_rtt300"],
         scenario="Same video stream at 150 ms one-way delay (NACK disabled): random 5% loss with block masks, and "
                  "Gilbert-Elliott bursty loss comparing interleaved against block masks. Frames must be complete "
                  "within the 250 ms budget.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "Recovery is iterative (peeling decoder): every recovered packet is fed back, which can complete "
               "other parity equations. Masks are 64-bit, so groups are capped at 48 packets. The wire format is "
               "this library's own (documented in the header), not RFC 8627 FlexFEC; the masking and recovery "
               "logic is the same.",
         verification="A unit test initially expected 2 recoveries but the decoder recovered 3: a packet was "
                      "rebuilt from parity before its own late arrival, which is correct, so the expectation was "
                      "fixed."),
    dict(id="3.3", slug="red", title="RED audio redundancy (RFC 2198)",
         problems=["Lost audio frames -> RED redundancy for audio"],
         headers=["loss/red.h"], unit=["Red"], e2e=["E2E_Red"],
         bench=["loss/RedEncodeParse_d2", "loss/Q_Red_10pct"],
         scenario="20 000 Opus-sized frames (60-90 B, 20 ms) over 10% random loss with redundancy distance 0, 1 "
                  "and 2. Residual loss is counted after RED recovery.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "Residual loss is roughly p^(d+1) for random loss, at a bitrate cost of about d x the primary "
               "payload. Redundant blocks are bounded to the 10-bit length and 14-bit timestamp offset of the "
               "RFC 2198 header.",
         verification="No defects found; results match the analytical p^2 and p^3. The test bench's `audio v2` (issue #126) "
                      "feeds it frames the size of libwebrtc's Opus output (`testbench/opus_model.h`): speech of 72 B on "
                      "average, digital silence of 3-35 B. Deviation: the sizes are a model fitted to libwebrtc's forward-link "
                      "trace, not libopus; the encoder defaults (20 ms, 32 kbps, VBR, DTX off, 6-510 kbps) follow "
                      "`audio_encoder_opus_config.h`."),
    dict(id="3.4", slug="plc", title="Packet loss concealment (PCM domain)",
         problems=["Lost audio frames -> PLC in NetEq (Opus in-band FEC / DRED are codec features, see notes)"],
         headers=["loss/plc.h"], unit=["Plc"], e2e=["E2E_Plc"],
         bench=["loss/Plc_Conceal10ms_16k", "loss/Q_Plc_10pct_speech"],
         scenario="20 s of synthetic voiced speech, 10 ms frames, 10% random loss. Compares the whole-signal SNR "
                  "of the concealed output against zero insertion, and the SNR/level inside the lost frames.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "Opus LBRR (in-band FEC) and DRED live inside the Opus codec and are used automatically when the "
               "codec is Opus; they are not reimplemented here. This module is the codec-agnostic fallback and "
               "the 'expand' operation of the audio jitter buffer (4.5).",
         verification="No defects found."),
    dict(id="3.5", slug="keyframe-requests", title="Keyframe requests (PLI/FIR) and loss notification",
         problems=["Lost or undecodable video frame -> PLI / FIR keyframe request; LNTF (loss notification)",
                   "Lost frame (decode impossible) -> keyframe request; freeze the last frame until recovery"],
         headers=["loss/keyframe_request.h", "loss/rtcp_feedback.h"], unit=["KeyframeRequest", "RtcpFeedback"], e2e=["E2E_Keyframe"],
         bench=["loss/KeyframeRequester_Frame", "loss/Q_Keyframe_2pct_frame_loss"],
         scenario="30 s of 30 fps video with an IPPP reference chain and 2% unrecoverable frame loss "
                  "(after NACK). The sender answers each PLI with a keyframe. Measures the decodable-frame ratio, "
                  "the longest freeze and the number of PLIs (no request storms).",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "The repair window (max(200 ms, 2 x RTT)) gives NACK a chance before a keyframe is requested; "
               "requests are throttled to max(100 ms, 1.5 x RTT) and retried after twice that. Wire formats (loss/"
               "rtcp_feedback.h): PLI (RFC 4585 6.3.1), FIR (RFC 5104 4.3.1, with its own request sequence number), "
               "loss notification (LNTF, libwebrtc's goog-lntf application-layer feedback) and compound RTCP with "
               "the report first; each is checked against libwebrtc's parsers in compare/webrtc/rtcp_interop.cc.",
         verification="Pass threshold derived analytically: each unrecoverable loss costs repair window + RTT + "
                      "keyframe transit, about 10 frames, so 2% loss bounds decodability near 80%. Without "
                      "keyframe requests only ~3% of frames decode."),
    dict(id="3.6", slug="temporal-layers", title="Temporal scalability (L1T2/L1T3)",
         problems=["A single loss breaking every later frame -> Temporal layers / SVC: dependent frames can be dropped safely"],
         headers=["loss/temporal_layers.h"], unit=["TemporalLayers"], e2e=["E2E_TemporalLayers"],
         bench=["loss/TemporalLayers_ScheduleAndFilter", "loss/Q_TemporalLayers_5pct"],
         scenario="Same frame-loss simulation at 5% loss with L1T1 versus L1T3 (keyframe requests on), plus an "
                  "SFU forwarding only T0+T1 and checking that every forwarded frame is decodable.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "Bitrate split follows libvpx defaults (40/20/40% for L1T3). Layer-sync frames (that depend only "
               "on T0) are where a receiver may switch up.",
         verification="No defects found."),
    dict(id="3.7", slug="protection-selection", title="NACK/FEC protection selection",
         problems=["Picking a protection level -> loss-rate-driven trade-off between FEC and NACK, based on RTT"],
         headers=["loss/protection_controller.h"], unit=["Protection"], e2e=["E2E_Protection"],
         bench=["loss/ProtectionController_Update", "loss/Q_Protection_adaptive_rtt300"],
         scenario="The NACK/RTX/FEC video simulation with the controller choosing FEC every 500 ms from measured "
                  "loss and burstiness, at RTT 20, 80 and 300 ms with 5% loss.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "The FEC rate is the smallest parity count that brings the binomial frame-loss probability under a "
               "target that moves from 10% (low RTT, NACK still helps) to 1% (high RTT). This is an analytical "
               "replacement for libwebrtc's lookup tables.",
         verification="No defects found. At low RTT the controller chooses NACK only (overhead equals the loss "
                      "rate); at 300 ms RTT it reaches 98.8% on-time delivery at about 27% overhead. "
                      "Video RED+ULPFEC is off beside NACK for H.264 and Generic, as libwebrtc's "
                      "`ShouldDisableRedAndUlpfec` decides (`rtp::ShouldDisableRedAndUlpfec`, "
                      "[fix record #7](../../fixes/7-no-fec-beside-nack.md)); the controller only runs where "
                      "libwebrtc would protect (NACK off, or VP8/VP9)."),
    # ------------------------------------------------------------------ 4
    dict(id="4.1", slug="packet-buffer", title="Packet buffer: reordering, de-duplication, frame assembly",
         problems=["Out-of-order packets -> sequence-number reordering in the packet buffer",
                   "Duplicate packets -> dedup by sequence number"],
         headers=["timing/packet_buffer.h"], unit=["PacketBuffer"], e2e=["E2E_PacketBuffer"],
         bench=["timing/PacketBuffer_InsertAssemble"],
         scenario="1 500 six-packet frames over a link with 8 ms Gaussian jitter (heavy reordering) and 3% "
                  "duplication, with sequence numbers crossing the 16-bit wrap. Every frame must be assembled "
                  "exactly once, with packets in order.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Frame boundaries come from the packetizer's start flag and the RTP marker bit; frames may complete "
               "out of order and are ordered later by decodability (3.5) and the frame scheduler (7.7). "
               "Version 2 (under `keyframe v2`, issue #76 group 6) sizes the buffer as libwebrtc's PacketBuffer: 512 slots "
               "doubling to 2048 when another unassembled packet maps to a slot, a clear plus a keyframe request when it is "
               "full at 2048, and a restart when a packet is 1024 behind the lowest sequence number. Packets leave at "
               "frame assembly, and the receiver also calls ClearTo(last sequence number) when a frame leaves the scheduler (issue #101): "
               "it drops the packets up to it and ignores older ones; padding is kept only within a quarter of the buffer behind an insert.",
         verification="No defects found."),
    dict(id="4.2", slug="rtcp-stats", title="RTCP statistics: loss, jitter and RTT",
         problems=["Measuring network delay -> RTCP SR/RR (RTT, jitter), transport-wide feedback, abs-send-time"],
         headers=["timing/rtcp_stats.h"], unit=["RtcpStats"], e2e=["E2E_RtcpStats"],
         bench=["timing/ReceiveStatistics_OnPacket"],
         scenario="60 s of 50 pps media over 30 ms + 5 ms jitter with 5% loss, with real SR/RR packets serialized "
                  "and parsed in both directions. RTT, fraction lost and jitter are compared with the configured "
                  "network.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Implements RFC 3550 A.3 (expected/lost/fraction) and A.8 (interarrival jitter, 1/16 gain) and "
               "the SR/RR wire format. Transport-wide feedback and abs-send-time are in 5.1 and 8.4. "
               "As libwebrtc's StreamStatisticianImpl (issue #76 group 5) the jitter filter runs in Q4 integers, only on "
               "in-order packets with a new timestamp, and skips a jump of 5 s of video clock; the RTT is floored at 1 ms "
               "(CompactNtpRttToTimeDelta, group 4). When the payload frequency changes the jitter is rescaled (ReviseFrequencyAndJitter, issue #101).",
         verification="No defects found; RTT min 60.6 ms against 60 ms configured."),
    dict(id="4.3", slug="delay-manager", title="Audio target delay estimation (DelayManager)",
         problems=["Variable packet arrival (audio) -> NetEq adaptive jitter buffer"],
         headers=["timing/delay_manager.h"], unit=["DelayManager"], e2e=["E2E_DelayManager"],
         bench=["timing/DelayManager_Update"],
         scenario="The audio jitter buffer simulation with jitter stepping from 5 ms to 40 ms half way; the target "
                  "delay must follow.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "A histogram with forgetting factor 0.983 per packet: rising jitter is learned within a few "
               "packets, while falling jitter takes several seconds, so the buffer does not oscillate.",
         verification="No defects found."),
    dict(id="4.4", slug="time-stretch", title="Time stretching (WSOLA accelerate / preemptive expand)",
         problems=["Variable packet arrival (audio) -> time-stretching: accelerate, preemptive expand",
                   "Clock drift between sender and receiver -> NetEq stretching"],
         headers=["timing/time_stretch.h"], unit=["TimeStretch"], e2e=["E2E_TimeStretch"],
         bench=["timing/TimeStretch_Accelerate30ms_16k", "timing/Q_TimeStretch_stall600ms"],
         scenario="Audio over a quiet network that stalls for 600 ms and then delivers the backlog at once. With "
                  "stretching the buffer must return to its target; without it the extra 600 ms of latency stays "
                  "for the rest of the call.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Accelerate removes one pitch period by cross-fading segment A into B; preemptive expand inserts "
               "one. Both are refused on unvoiced, non-silent audio (correlation < 0.75), where they would be "
               "audible.",
         verification="Defect found in integration: the jitter buffer only decoded audio until it had 10 ms "
                      "ready, but WSOLA needs 30 ms, so stretching never ran and latency stayed at 600 ms after "
                      "a stall. Fixed by decoding ahead only when a stretch is wanted (AudioJitterBuffer::"
                      "MaybeStretch); recovery now takes ~0.5 s."),
    dict(id="4.5", slug="audio-jitter-buffer", title="Adaptive audio jitter buffer (NetEq-lite)",
         problems=["Variable packet arrival (audio) -> NetEq adaptive jitter buffer with time-stretching"],
         headers=["timing/audio_jitter_buffer.h"], unit=["AudioJitterBuffer"], e2e=["E2E_AudioJitterBuffer"],
         bench=["timing/AudioJitterBuffer_10msPull_16k", "timing/Q_AudioJitterBuffer_jitter25_loss2"],
         scenario="60 s of speech, 20 ms packets, 25 ms Gaussian jitter (with reordering), 2% loss. The adaptive "
                  "buffer is compared with a fixed 20 ms and a fixed 200 ms buffer.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Decoded PCM is inserted (decode-on-insert); a decoder interface that decodes at pull time (to use "
               "Opus FEC from the next packet) is a natural extension. Statistics use WebRTC getStats names.",
         verification="See 4.4 for the decode-ahead defect. A first comparison test was wrong: a fixed 20 ms "
                      "buffer without acceleration does not conceal late packets, it waits for them, so its "
                      "latency ratchets up (to 4x its target). The test now checks concealment against the loss "
                      "rate and delay against both fixed buffers."),
    dict(id="4.6", slug="video-jitter", title="Video jitter estimation and render timing",
         problems=["Variable packet arrival (video) -> video jitter buffer, frame buffer and render-time estimation"],
         headers=["timing/video_jitter_estimator.h"], unit=["VideoJitter"], e2e=["E2E_VideoJitter"],
         bench=["timing/VideoJitterEstimator_Update", "timing/Q_VideoJitter_late_frames"],
         scenario="60 s of 30 fps video with 3-6 KB frames and a 25 KB keyframe every 2 s over a 2.5 Mbps "
                  "bottleneck with 10 ms jitter. Each frame's render time comes from VideoTiming fed by the "
                  "estimator; a frame is late if it arrives (plus decode time) after its render time.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Deviation from libwebrtc: the jitter term is 3 sigma with no offset, where libwebrtc uses "
               "2.33 sigma - 30 ms (it compensates elsewhere with OS-jitter and render allowances). Both are "
               "configurable.",
         verification="Defect found: with libwebrtc's -30 ms offset the jitter delay collapsed to 1 ms for 15 ms "
                      "network jitter, so about 600 of 1 800 frames arrived after their render time. With the new "
                      "default, late frames fell from 2.8% to 0.1% in the timing e2e test, at 134 ms mean delay. "
                      "Provisional lock (Owner, PR #180): `issue_76_tcc_v2_high_jitter` holds video e2e delay p95 "
                      "at < 1300 ms (libwebrtc 136 ms) until the receive-side delay under jitter with reordering is "
                      "fixed in #24."),
    dict(id="4.7", slug="clock-drift", title="Clock drift estimation and compensation",
         problems=["Clock drift between sender and receiver -> sample-rate drift compensation in the audio device module"],
         headers=["timing/clock_drift.h"], unit=["ClockDrift"], e2e=["E2E_ClockDrift"],
         bench=["timing/DriftCompensator_10ms_16k", "timing/Q_ClockDrift_300ppm"],
         scenario="120 s of audio from a sender whose clock is 300 ppm slow, over 40 ms + 5 ms jitter. The "
                  "receiver plays 10 ms blocks from a FIFO; with compensation, the PI-controlled resampler holds "
                  "the FIFO at 60 ms.",
         notes="Using the per-window minimum transit time (lower envelope) instead of every packet makes the "
               "drift fit almost immune to jitter. The resampler ratio is limited to +-0.5%.",
         verification="Defect found: the first PI gains gave a ~500 s time constant (slow oscillation, 15.8 ms "
                      "error). Retuned from the plant model (the buffer integrates at 10 ms per update) to a "
                      "~3 s, near critically damped loop, with an EMA on the packet-granular level: the error "
                      "fell to 1.8 ms."),
    # ------------------------------------------------------------------ 5
    dict(id="5.1", slug="transport-feedback", title="Transport-wide CC feedback (and REMB)",
         problems=["Fine-grained per-packet feedback -> transport-cc (Transport-Wide CC) RTCP feedback",
                   "Receiver-side bandwidth hints -> REMB (legacy)"],
         headers=["cc/transport_feedback.h"], unit=["TransportFeedback"], e2e=["E2E_TransportFeedback"],
         bench=["cc/TransportFeedback_Build100_SerializeParse"],
         scenario="7 500 packets over 5% loss with jitter (reordering); feedback every 50 ms serialized to the "
                  "real wire format, parsed and joined with the send history. Each packet must be reported once, "
                  "with arrival-time error within the 250 us quantization.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Status chunks use run-length coding for runs of 14 or more, otherwise 1-bit or 2-bit status "
               "vectors. A message is split when a delta overflows 16 bits.",
         verification="No defects found; maximum arrival-time error 249 us."),
    dict(id="5.2", slug="delay-based-bwe", title="Delay-based bandwidth estimation (GCC)",
         problems=["Unknown or changing available bandwidth -> GCC: delay-based estimation"],
         headers=["cc/delay_based_bwe.h", "cc/aimd_rate_control.h"], unit=["DelayBwe", "DelayBweV2", "DelayBweLibwebrtc", "RobustThroughputEstimator", "AcknowledgedBitrateEstimator", "AimdRateControl", "BitrateEstimatorV2"], e2e=["E2E_DelayBwe"],
         bench=["cc/Gcc_OnFeedback_per_packet", "cc/Q_Gcc_capacity_steps"],
         scenario="Full closed loop (encoder, pacer, transport-cc, GCC) over a bottleneck whose capacity steps "
                  "2000 -> 800 -> 2500 kbps every 20 s, with a 200 KB drop-tail queue. Utilization and queueing "
                  "delay are measured over the second half of each phase.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "After a capacity increase, GCC climbs multiplicatively at 8%/s. Going from 650 to 2500 kbps "
               "takes about 17 s, so the last phase has a lower utilization bar. "
               "`cc v2` (`GccConfig::version`, issue #19) starts the port of libwebrtc's GoogCC as a unit (#61): the "
               "detector's front end is `DelayBasedBwe::IncomingPacketFeedbackVector` with `InterArrivalDelta` "
               "(`BelongsToBurst`, reordered groups, feedback sorted by receive time, 2 s stream time-out). The "
               "trendline detector was already libwebrtc's. #15 adds the rest of the overuse path to `cc v2`: the "
               "acknowledged rate (#15: `BitrateEstimator`, 500 ms first window, then 150 ms Bayesian windows; since #150 `RobustThroughputEstimator`, see below), "
               "`AimdRateControl` (`aimd_rate_control.h`: near-max additive increase, decrease with `beta x "
               "throughput - 5 kbps`, `LinkCapacityEstimator`, `TimeToReduceFurther`, `bitrate_is_initialized_`) and "
               "`DelayBasedBwe::MaybeUpdateEstimate` (halve every `clamp(rtt, 10, 200)` ms when overusing without an "
               "acked rate; a probe result sets the estimate as is). #48 adds `AlrDetector` (`alr_detector.h`: the "
               "ALR state marks the acked-rate estimator, and the end of an ALR calls `ExpectFastRateChange`), "
               "the probe controller and probe estimator (5.6), and `recovered_from_overuse`, which requests a probe "
               "after a big drop. #166 ports `delay_based_bwe_unittest.cc` (20 cases, `DelayBweLibwebrtc`; found that `Result.updated` "
               "is `ValidEstimate()`, not always true) and `robust_throughput_estimator_unittest.cc`; `acknowledged_bitrate_estimator_unittest.cc` (3 cases, `AcknowledgedBitrateEstimator`, with a recording `BitrateEstimator` in place of the gmock) follows. M157's acknowledged-bitrate "
               "estimator is `RobustThroughputEstimator` (`RobustThroughputEstimatorSettings::enabled = true`, "
               "`AcknowledgedBitrateEstimatorInterface::Create`), and since #150 `cc v2` feeds it with the receive-sorted "
               "feedback instead of the `BitrateEstimator` (the ALR marks are no-ops there, as in libwebrtc). Not ported, because we use "
               "transport-cc with send-side estimation: the receive-side `remote_bitrate_estimator_*`, `inter_arrival`, "
               "`overuse_detector` and `congestion_control_feedback_*` tests. Remaining differences: `prior_unacked_data` is zero, no `NetworkStateEstimate`.",
         verification="Defects found. (1) The AIMD controller reduced the rate on every feedback while overuse "
                      "lasted (4 cuts in 500 ms); a libwebrtc-style guard now allows one decrease per RTT unless "
                      "throughput collapses. (2) The simulator's periodic 3x keyframes every 3 s triggered "
                      "regular overuse and held utilization near 50%; real WebRTC sends keyframes on request, "
                      "and so does the simulator now. Utilization rose to ~80%, with queue p95 of 20-33 ms."),
    dict(id="5.3", slug="loss-based-bwe", title="Loss-based bandwidth estimation",
         problems=["Unknown or changing available bandwidth -> GCC: loss-based estimation"],
         headers=["cc/loss_based_bwe.h", "cc/loss_based_bwe_v2.h", "cc/send_side_bandwidth_estimation.h"],
         unit=["LossBwe", "LossBweV2", "SendSideBweV2", "GccV2Loss"], e2e=["E2E_LossBwe"],
         bench=["cc/Q_LossBwe_15pct"],
         scenario="An 8 Mbps link with 15% versus 1% random loss: heavy loss must push the rate far below "
                  "capacity, light loss must not.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Deviation from libwebrtc v1: below 2% loss this controller does not limit at all (delay-based "
               "and probing own the upward direction), whereas libwebrtc caps increases at 8%/s after start-up. "
               "`cc v2` (#12) is libwebrtc's code instead: `LossBasedBweV2` (`loss_based_bwe_v2.h`, CreateConfig's "
               "defaults) fits the loss model p = q + (1 - q)(R - B)/R to 15 observations of at least 250 ms "
               "(byte loss rate, weights 0.9^age) by one Newton step per candidate bandwidth (current x 1.02 / "
               "1.0 / 0.95, the acked rate, a higher delay-based estimate), bounds the result by the instant upper "
               "bound 100 kbps / (loss - 5%) and the acked rate, holds after a decrease (300 ms doubling to 60 s), "
               "limits increases to 1.3x per 300 ms and 1.5x the acked rate, and reports the state the probe "
               "controller reads. A packet counts as lost only when a feedback covers it and reports it not "
               "received; a later report as received removes it again. `SendSideBandwidthEstimation` "
               "(`send_side_bandwidth_estimation.h`) owns the target: it takes the loss-based result once three "
               "observations exist, trusts the delay-based estimate in the first 2 s after the first receiver "
               "report, falls back to the receiver-report loss rule before that, and cuts x0.8 per second when "
               "the propagation RTT is above 3 s. Remaining differences: the report-block loss deltas come from "
               "`rtp::Session` (not libwebrtc's `RtpTransportControllerSend`, which is not in `reference/`, #57), "
               "no field trials, and `ProbeController` reads the loss-based state but not "
               "`LossBasedState::kIncreaseUsingPadding` padding (the pacer sends probes, not loss-based padding).",
         verification="Defect found: loss was measured over 20-packet intervals, and a lucky interval with no "
                      "loss (about 4% likely at 15% loss) let the estimate jump from 153 to 2 338 kbps. Now loss "
                      "windows are at least 1 s and decisions are applied to the current target. `cc v2`: the "
                      "unit tests port libwebrtc's `loss_based_bwe_v2_test.cc` cases with their numbers; the "
                      "scenarios `issue_12_lossbwe_*` check 5% random loss (utilization 76-82% against libwebrtc's "
                      "76%), burst loss, a policer, a shallow buffer and congestive loss, and a lossless jittery "
                      "link."),
    dict(id="5.4", slug="gcc-controller", title="Send-side bandwidth estimation (GCC network controller)",
         problems=["Unknown or changing available bandwidth -> GCC (delay-based and loss-based estimation)"],
         headers=["cc/send_side_bwe.h"], unit=["SendSideBwe", "GoogCcNetworkController"], e2e=["E2E_SendSideBwe"],
         bench=["cc/Gcc_OnFeedback_per_packet"],
         scenario="A 1.5 Mbps link through three 20 s phases: clean, 5% loss (GCC 'hold' band) and 20% loss "
                  "(decrease band). Each phase's target is compared with the previous one.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "The target is min(delay-based, loss-based, REMB), clamped to [min, max]; the pacing rate is "
               "2.5 x target. rtp::SendSideController adds congestion-window pushback "
               "(cc/congestion_window_pushback.h, a port of libwebrtc's CongestionWindowPushbackController with "
               "its drop-frame-only mode): when the data in flight exceeds target x (min feedback RTT + 350 ms) "
               "the encoder drops every N-th captured frame (SendRates::frame_drop_interval, "
               "SendSideController::DropCapturedFrame()). The pacing rate is not reduced. "
               "#166 ports goog_cc_network_control_unittest.cc against `GccController` with `cc v2` "
               "(`GoogCcNetworkController`, 8 cases): the 7 `GoogCcNetworkControllerTest` cases the API can express pass, "
               "`OnNetworkRouteChanged` in its first half (a route change without a starting rate has no counterpart: "
               "`ResetEstimate` always takes a rate, and keeps the configured min and max), `ProbeOnRouteChange` with the "
               "route's maximum configured up front, and `TriggersTargetRateUpdateWhenBandwidthLimitedChanges` on the "
               "`AlrDetector` state (only libwebrtc's RFC 8888 feedback generator reads `is_bandwidth_limited`). "
               "`GoogCcScenario.NoCrashOnVeryLateFeedback` is ported at the controller level. Not ported: "
               "`RespectsConfiguredPacingFactor` (there is no `StreamsConfig::pacing_factor`; see #187), "
               "`ProbeAfterRouteChangeWhenTransportWritable` (network availability belongs to the host stack's ICE), "
               "`LimitPacingFactorToUpperLinkCapacity` (a field trial, off in M157), and the other `GoogCcScenario` "
               "cases, which need libwebrtc's network emulation: the scenario suite and `sm_compare` cover their links "
               "(capacity steps, high loss, cross traffic, REMB caps), and the rest set field trials that are off by default "
               "(`WebRTC-CongestionWindow` sizes, `MaxRttLimit`, `SafeResetOnRouteChange`, `ResetOnAdapterIdChange`, "
               "`LossBasedControl`, `AddPacingToCongestionWindowPushback`) or send without transport-cc.",
         verification="Covered by the 5.2 and 5.3 fixes. Pushback (issue #53, "
                      "[fix record](../../fixes/53-cwnd-pushback.md)): SendHistory counted packets of a lost "
                      "transport feedback as in flight for 60 s, which would hold the window shut; they are now "
                      "settled once a later feedback is reported. The library default is on (libwebrtc enables it "
                      "through the WebRTC-CongestionWindow field trial; rate_control_settings.cc, which holds the "
                      "default, is not in compare/webrtc/reference)."),
    dict(id="5.5", slug="pacer", title="Pacer with priority queues",
         problems=["Bursts filling router queues (bufferbloat) -> Pacer: smooths packet sends, with priority queues",
                   "Protecting audio over video -> audio prioritized in the pacer; video degrades first"],
         headers=["cc/pacer.h", "cc/pacing_controller.h"], unit=["Pacer", "PacingController", "PrioritizedPacketQueue", "PacketRouter"], e2e=["E2E_Pacer"],
         bench=["cc/Pacer_EnqueueProcess", "cc/Q_Pacer_audio_delay"],
         scenario="The closed-loop simulation at 2.5 Mbps with and without pacing; audio and video queueing "
                  "delay (p95) are compared.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Two versions sit behind `PacerConfig::version` (test bench directive `pacer v1|v2`): v1, the library's "
               "own media-debt pacer (5 ms burst, drain on the oldest packet), is the default; v2 is a port of "
               "libwebrtc's PacingController and PrioritizedPacketQueue (M157, issue #70) and becomes the default "
               "together with the GoogCC port (#61), because with v1 estimator it gives a result neither has. v2: a 40 ms "
               "burst interval (capped at 63 kB), media debt capped at 500 ms of the rate, 2 s cap on the time "
               "credited, a queue-time limit of 2 s judged on the average queue time, audio unpaced but "
               "accounted, queue levels audio / retransmission / video+FEC / padding, keep-alive padding every "
               "500 ms while paused or congested, and padding at the padding rate 5 ms at a time. The first "
               "packet of a keyframe flushes the queued video, FEC and retransmission packets, as "
               "`PacingController::EnqueuePacket` does (the sender marks keyframes and first-of-frame packets; "
               "#5). With `rtx v2` the padding is made by `RtpSender::GeneratePadding` (`PacketRouter::GeneratePadding`): RTX payload "
               "padding from the history first, then 255-byte empty padding packets on the RTX SSRC, each with its real size "
               "in the pacer queue; the keep-alive and the first packet of a probe ask for 1 byte (#120). Without it the pacer "
               "queues 224 B placeholders. Remaining differences: round-robin between SSRCs of one level (one stream per kind in a "
               "one-to-one call), the packet TTLs (off by default), `SetCongested` is not driven by the "
               "congestion window (pushback stays on the encoder). `BitrateProber` (#48, `bitrate_prober.h`) times "
               "the probe bursts: a cluster starts when a media packet of at least min(`rate x min_probe_delta`, "
               "200 B) is enqueued, begins with a small padding packet, sends `rate x min_probe_delta` bytes per "
               "burst (padding fills it when there is no media), times the next burst by the bytes sent at the "
               "cluster rate and drops a cluster whose burst is more than 10 ms late (at most 5 pending, 5 s "
               "old). With `cc v2` the pacer is also scheduled as `TaskQueuePacedSender` does (`Pacer::ProcessV2`, #123): a "
               "pass runs only when a packet, a rate or the pause state changed or the timer of the last pass is due, "
               "and only while `NextSendTime()` has come, with 5 ms (3 packets) between timer passes. Polling a bare "
               "`PacingController` every millisecond lets the early-return branch of `ProcessPackets` re-base its "
               "debt every call and sends queued packets between the audio packets instead of with them.",
         verification="The unit tests port the intent of libwebrtc's pacing_controller_unittest.cc (burst "
                      "interval, debt cap, priorities, long-queue drain on the average queue time, padding, "
                      "congestion keep-alives). `PrioritizedPacketQueue` (#166) ports prioritized_packet_queue_unittest.cc: "
                      "12 of its 19 cases are ported and pass; the other 7 cover options that are off by default "
                      "(audio-retransmission priority, `fast_retransmissions`, the per-level TTLs) or the round robin "
                      "between SSRCs, which a one-to-one call does not need. Queue sizes are wire sizes, where libwebrtc's "
                      "`SizeInPayloadBytes` counts payload and padding only (it feeds the long-queue drain rate). "
                      "`PacketRouter` (#166) ports packet_router_unittest.cc against `rtp::SendSideController`, "
                      "`rtp::RtpSender` and `rtp::Session`, which share the router's work: padding with no stream, RTX "
                      "payload padding on the RTX SSRC, video preferred over audio for padding, RTP sequence numbers at "
                      "release, and transport-wide sequence numbers. The port found two differences, now fixed behind "
                      "`pacer v2`: numbering starts at 1 (`transport_seq_(1)`), and a packet without the transport-cc "
                      "extension gets no number and is not reported to GCC (`NotifyBweOfPacedSentPacket`); neither "
                      "changes a scenario, which all map the extension. Not ported: RFC 8888 numbering "
                      "(`SetGenerateTransportSequenceNumbers`), `allow_sending` and ECT(1), which are off by default; "
                      "REMB and RTCP routing (`SendRemb`, `SendCombinedRtcpPacket`; the session's `RtcpEngine` sends "
                      "from its one local SSRC); module registration and its death tests, `PadsOnLastActiveMediaStream`, "
                      "`OnBatchComplete` and `GetRtxSsrcForMedia` between modules, which only matter with several send "
                      "streams of one kind (a one-to-one call has one audio and one video stream); and "
                      "`OnAbortedRetransmissions`, which M157's pacer never calls. "
                      "`TaskQueuePacedSender` (#166) ports task_queue_paced_sender_unittest.cc against `Pacer` v2 with "
                      "its task-queue scheduling: 11 of its 14 cases in full and 3 in part, all passing (pacing over a second, "
                      "the burst window, rescheduling on a rate change, audio sent at once, the fixed and packet-based "
                      "coalescing windows, probes overriding them, probes 200 us apart, a probe ending in the send loop, "
                      "the oldest wait and the queue size). The port found two differences, now fixed behind `pacer v2`: "
                      "the pacer's `DataSize / DataRate` and `DataSize / TimeDelta` rounded up, where libwebrtc's "
                      "(api/units/data_rate.h) truncate, so a probe burst due 0.3 us past the 1 ms early-probe margin was "
                      "held back (`NoMinSleepTimeWhenProbing`); and the packet-based hold back was sized by the "
                      "configured pacing rate, where `MaybeProcessPackets` uses `pacing_rate()`, the rate after the "
                      "long-queue boost. The first changes `issue_123_v2_allocation_min`, whose ten-seed means move both "
                      "ways (4 metrics toward libwebrtc's, 5 away, all by small amounts), and `issue_48_prober_low_bandwidth` "
                      "slightly; the second changes no scenario. Not ported: `RemovePacketsForSsrc` "
                      "(the library removes a stream's packets only by the keyframe flush), `FirstSentPacketTime` and "
                      "`ExpectedQueueTime` (not exposed by `Pacer`), and the rate change inside the `SendPacket` call "
                      "stack, which `Pacer` cannot have because it returns the packets it sends. "
                      "With the 40 ms burst a v2 frame leaves in one burst, so the "
                      "E2E_Pacer v2 case expects that the paced run is not worse than the unpaced one on audio delay "
                      "(audio is not paced in libwebrtc either; v1 keeps the original expectation). The "
                      "pacer_v2_capacity_drop scenario keeps the v2 run reproducible; issue_5_keyframe_flush locks in "
                      "the keyframe flush (docs/fixes/5-pacer-keyframe-flush.md). See "
                      "docs/design/libwebrtc-parity.md 5.5 and PR #80 for the suite comparison."),
    dict(id="5.6", slug="probing", title="Bandwidth probing",
         problems=["Probing for more bandwidth safely -> bandwidth probing (padding / RTX probe clusters)"],
         headers=["cc/probing.h", "cc/bitrate_prober.h", "cc/alr_detector.h"],
         unit=["Probing", "BitrateProber", "PacingControllerProbing", "AlrDetector", "ProbeControllerV2", "ProbeBitrateEstimatorV2", "GccV2Probing"],
         e2e=["E2E_Probing"],
         bench=["cc/Q_Probing_rampup_5mbps"],
         scenario="A 5 Mbps link starting at 300 kbps: time for the target to reach 80% of capacity, with and "
                  "without probing.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Probe clusters are sent by the pacer at the probe rate, using padding when there is no media; "
               "the estimator ignores clusters with too few packets or bytes. `cc v2` (#48) is libwebrtc's code: "
               "`ProbeControllerV2` (`ProbeController`: clusters at 3x and 6x the start rate, further probes at 2x "
               "a result above 0.7 of the last probe, probes when the max or max allocated bitrate rises, "
               "`RequestProbe` after a big drop within 5 s in or just after an ALR, periodic ALR probes only when "
               "the stream requests them, none while the estimate is limited by delay increase or loss), "
               "`ProbeBitrateEstimator` in libwebrtc mode (receive/send ratio over 2 rejected, clusters expire "
               "after 1 s, the newest estimate wins), `AlrDetector` and `BitrateProber` (in `pacer v2`). "
               "The loss-based state and the RTT limit decide the bandwidth-limited cause (#12, `GetBandwidthLimitedCause`). "
               "Remaining differences: no `NetworkStateEstimate`, and `cc v1` keeps the old controller until #61. "
               "The `GoogCcNetworkController` glue (#61) is in `cc v2`: `GetPacingRates` (2.5 x the target until the first "
               "feedback, 1.1 x after; padding rate), the packet-overhead rate off the video allocation "
               "(`RtpVideoSender::OnBitrateUpdated`) and the congestion-window frame drop against the video target. With "
               "libwebrtc's default `CongestionWindowConfig` the window never reaches the pacer (`SetCongested` stays "
               "false), as in libwebrtc.",
         verification="No defects found. Ramp-up takes 0.2 s with probing; without it, 80% is not reached "
                      "within 30 s."),
    dict(id="5.7", slug="bitrate-allocator", title="Bitrate allocation among streams",
         problems=["Sharing bandwidth among streams -> bitrate allocator, with per-stream priority and min/max bitrate"],
         headers=["cc/bitrate_allocator.h", "cc/bitrate_allocator_v2.h"], unit=["BitrateAllocator", "BitrateAllocatorV2"], e2e=["E2E_BitrateAllocator"],
         bench=["cc/BitrateAllocator_5streams", "cc/Q_Allocator_low_bandwidth"],
         scenario="Audio (enforced 20-40 kbps), camera (150-2500 kbps) and screen share (250-1500 kbps, priority "
                  "2) share a GCC target while capacity steps 3000 -> 450 -> 3000 kbps.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Paused streams resume only at min x 1.1 (hysteresis). "
               "`allocator v2` (`SendSideConfig::allocator_version`, issue #104) is the port of libwebrtc's `BitrateAllocator` "
               "(`call/bitrate_allocator.cc`): `LowRateAllocation` / `NormalRateAllocation` / `MaxRateAllocation` with min + max(10 %, 20 kbps) "
               "and the protection share to resume, and the sum of the enforced minimums (audio, and video as `suspend_below_min_bitrate` is off) is "
               "the lower bound of GCC's target (`min_total_allocated_bitrate`, `use_min_allocatable_as_lower_bound_`). "
               "Remaining: the elastic allocation (off by default in libwebrtc), the padding limit (`max_padding_rate`, with the prober, #48), "
               "the per-packet overhead libwebrtc takes off the video payload rate, and the audio min and max from Opus's frame-length range (not in `reference/`, #57).",
         verification="A unit-test expectation was wrong about how leftover bitrate is shared (it is split by "
                      "priority between audio and screen); the expectation was corrected."),
    dict(id="5.8", slug="frame-dropper", title="Encoder overshoot control (frame dropper)",
         problems=["Encoder bitrate overshoot -> encoder rate control, frame dropper"],
         headers=["cc/frame_dropper.h", "cc/encoder_bitrate_adjuster.h"], unit=["FrameDropper", "EncoderOvershootDetector", "EncoderBitrateAdjuster"], e2e=["E2E_FrameDropper"],
         bench=["cc/FrameDropper_and_Overshoot", "cc/Q_FrameDropper_overshoot160"],
         scenario="An encoder that overshoots its target by 60% in the closed loop, with and without the "
                  "frame dropper; the sent video rate is compared with the target.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "FrameDropper is a port of libwebrtc's modules/video_coding/utility/frame_dropper.cc (M157) with its "
               "constants: 0.5 s leaky bucket, accumulator cap 3 s, drop-ratio, key-frame-ratio and frame-size ExpFilters, "
               "keyframes and delta frames over 3x the average spread over 0.5 s of frames, at most 4 s of frames dropped "
               "in a row, and the accumulator rescaled by new/old target when the target drops. Keyframes are not "
               "exempt, as in VideoStreamEncoder::OnFrame. The session layer's rtp::VideoSendController "
               "(issue #37, docs/design/send-controller.md) drives it per captured frame (Leak, DropFrame, SetRates, Fill). "
               "The input frame rate is measured as FrameCadenceAdapter does (RateStatistics over 2970 ms, "
               "VideoStreamEncoder::GetInputFramerateFps; the configured maximum while it is unavailable); "
               "RateStatistics (rtc_base/rate_statistics) and ExpFilter (rtc_base/numerics/exp_filter) are checked "
               "against the sources now in compare/webrtc/reference. The encoder's "
               "overshoot correction is a port of libwebrtc's "
               "video/encoder_overshoot_detector.cc and encoder_bitrate_adjuster.cc (cc/encoder_bitrate_adjuster.h; "
               "issue #5): the encoder is configured with the target lowered by the measured overshoot. It runs in "
               "VideoSendController and is on by default as in M157 (RateControlSettings bitrate_adjuster = true, "
               "adjuster_use_headroom = true; `bitrate_adjuster off` in a scenario). The link allocation is "
               "VideoSendStreamImpl::OnBitrateUpdated's, which equals the encoder target, so the adjuster's headroom "
               "is only what the encoder allocation adds (zero here). Remaining differences (issue #5): the media "
               "utilization factor comes from the detector, not RateUtilizationTracker "
               "(rtc_base/rate_utilization_tracker); one stream with one temporal layer; rates adjusted per captured "
               "frame, not per bitrate update. The VideoStreamEncoder has no keyframe budget of its own: a keyframe "
               "request only sets next_frame_types_ (sticky until a keyframe is encoded, as here) and the keyframe "
               "then goes through the frame dropper like any frame.",
         verification="Issue #53 ([fix record](../../fixes/53-cwnd-pushback.md)): keyframe bytes entered an uncapped "
                      "bucket and a run of keyframe requests made the dropper drop every delta frame for seconds. "
                      "Issue #5 ([fix record](../../fixes/5-frame-dropper-port.md)): replaced by the libwebrtc "
                      "port (3 s cap, rescale on a rate drop); the 1.5 s cap and the keyframe exemption are gone."),
    dict(id="5.9", slug="video-adaptation", title="Resolution/frame-rate adaptation and CPU overuse detection",
         problems=["Low bandwidth -> adaptive resolution / framerate degradation (degradationPreference)",
                   "CPU too slow to encode -> CPU overuse detector, which lowers resolution or fps"],
         headers=["cc/video_adaptation.h"], unit=["VideoAdaptation"], e2e=["E2E_VideoAdaptation"],
         bench=["cc/Degradation_and_CpuDetector", "cc/Q_Adaptation_bandwidth_drop"],
         scenario="(a) Closed loop with capacity 3000 -> 400 -> 3000 kbps under maintain-framerate: the resolution "
                  "must drop and recover. (b) A slow device (40 ms of encode per megapixel): the CPU detector must "
                  "settle on a sustainable resolution without oscillating.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "The resolution ladder alternates 3/4 and 2/3 linear scale steps (as libwebrtc's VideoAdapter); "
               "bpp_down/bpp_up hysteresis is 0.04/0.06 bits per pixel.",
         verification="No defects found after the 5.2 fixes."),
    dict(id="5.11", slug="outage", title="Link outage detection and recovery",
         problems=["Link outage, then recovery -> encoder pause, then keyframe on recovery; ICE restart if needed"],
         headers=["cc/outage_monitor.h"], unit=["Outage"], e2e=["E2E_Outage"],
         bench=["cc/Q_Outage_recovery"],
         scenario="A 2 Mbps call with a 7 s total outage (both directions) at 20 s; packets sent into the dead "
                  "link, ICE restarts and time to recover 50% of capacity are measured.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "On recovery the estimate is reset to half the last good rate and probing is restarted, because "
               "the path may have changed. libwebrtc has no outage monitor, so with `cc v2` the "
               "`SendSideController` does not run it (issue #151): the encoder keeps running, the congestion "
               "window drops frames, the pacer keeps sending, and the receiver's decode time-out asks for the "
               "keyframe once the link is back.",
         verification="No defects found. Under `cc v2` (issue #151, PR #184, Owner sign-off) "
                      "`issue_151_outage_3s_v2` seed 1 lands on libwebrtc (freeze 3229 vs 3221 ms, longest audio "
                      "gap 2980 vs 2977 ms); `issue_133_all_v2_outage_15s` `video.pause_total_ms` moves to < 18500 "
                      "(libwebrtc 18218 on seed 1). Explained difference (Owner, PR #184), owned by #24: on "
                      "seeds 2 and 3 of `issue_151_outage_3s_v2` the receiver's decode-time-out PLI reaches the sender "
                      "before the next capture, so that frame becomes the keyframe before the NACK arrives and the "
                      "resends queue behind it (freeze 4666 / 3948 ms, repair 6.30 / 3.18 %; main 4067 / 4047 ms, "
                      "1.43 / 1.26 %; libwebrtc 3256 / 3175 ms, 0.36 / 0.53 %). libwebrtc's PLI lands after that "
                      "capture because its frame release (FrameDecodeTiming) comes later; the other 8 of 10 seeds "
                      "and the audio gap on all 10 match libwebrtc."),
    # ------------------------------------------------------------------ 6
    dict(id="6.1", slug="echo-canceller", title="Acoustic echo cancellation",
         problems=["Echo (far-end audio coming back through the mic) -> AEC3 acoustic echo canceller; AECM for mobile"],
         headers=["audio/echo_canceller.h"], unit=["EchoCanceller"], e2e=["E2E_EchoCanceller"],
         bench=["audio/EchoCanceller_10ms_80ms_tail", "audio/FullCaptureChain_10ms",
                "audio/Q_EchoCanceller_full_duplex_call"],
         scenario="A 30 s two-party call: far-end speech played into a simulated room (40 ms response, RT60 "
                  "100 ms, 60 ms bulk delay), far end alone for 15 s, then double talk. ERLE is measured while "
                  "only the far end talks, and near-end preservation during double talk, through HPF -> AEC -> NS.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. This is a compact PBFDAF canceller (in the spirit of Speex MDF), not a port of AEC3. Blocks are "
               "5 ms, with 16 partitions (80 ms tail) after bulk-delay alignment by 6.2.",
         verification="Several defects were found and fixed. (1) The delay estimate was wrong (see 6.2), which "
                      "capped ERLE at 12 dB. (2) Double talk corrupted the filter: the background filter partly "
                      "'cancels' near speech for a few blocks and was then copied to the foreground. An "
                      "ERLE-based double-talk detector was tried and rejected (it fired on 40% of far-end-only "
                      "blocks). The final design uses Speex's statistical copy rule plus Valin's adaptive step "
                      "size, which collapses during double talk (step 0.35 -> 0.002): near-end SNR went from "
                      "-4 to +9 dB. (3) After an echo-path change the convergence state was not reset, so the "
                      "adaptive step stayed near zero and ERLE never recovered (0 dB); it now recovers to "
                      "25.6 dB."),
    dict(id="6.2", slug="echo-delay", title="Echo-path delay estimation",
         problems=["Delay between the speaker and mic paths -> echo-path delay estimation in AEC3"],
         headers=["audio/echo_delay_estimator.h"], unit=["EchoDelay"], e2e=["E2E_EchoDelay"],
         bench=["audio/EchoDelayEstimator_10ms", "audio/Q_EchoDelay_route_change"],
         scenario="A far-end-only call where the echo delay jumps from 60 to 180 ms at 12 s (speaker -> "
                  "Bluetooth). The AEC must re-estimate the delay and re-converge.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The search range is 500 ms; updates run every 100 ms and only while the far end is active.",
         verification="Defect found: with rectangular windows, the window edges correlated with each other and "
                      "produced false peaks at both ends of the lag range (0 and 8000 samples). Fixed with "
                      "Hann/Tukey tapering and a partial phase transform (|C|^0.8). The synthetic room response "
                      "was also made physical (direct path dominant)."),
    dict(id="6.3", slug="noise-suppression", title="Noise suppression",
         problems=["Background noise -> NS noise suppression (RNNoise-style ML in some builds)"],
         headers=["audio/noise_suppressor.h"], unit=["NoiseSuppressor"], e2e=["E2E_NoiseSuppressor"],
         bench=["audio/NoiseSuppressor_10ms", "audio/Q_NoiseSuppressor_7dB_snr"],
         scenario="Near-end speech in white noise at about 7 dB SNR through HPF -> NS; SNR against the clean "
                  "talker (passed through the same HPF) before and after.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. Classic statistical NS (Wiener + decision-directed SNR); an ML suppressor (RNNoise) could be "
               "plugged into the same 10 ms interface.",
         verification="The first chain test measured SNR against the raw talker, so the HPF's low-frequency "
                      "phase shift counted as noise; the reference now goes through the same HPF. NS raises SNR "
                      "from 7.6 to 12.9 dB."),
    dict(id="6.4", slug="gain-control", title="Automatic gain control and limiter",
         problems=["Volume too quiet, too loud, or clipping -> AGC2 automatic gain control, limiter, clipping predictor"],
         headers=["audio/gain_control.h"], unit=["GainControl"], e2e=["E2E_GainControl"],
         bench=["audio/Agc_10ms"],
         scenario="A quiet talker (~-33 dBFS) for 20 s, then a loud one (+20 dB). Quiet speech must reach "
                  "-18 dBFS; loud speech must not be amplified and every peak must stay under -1 dBFS.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. As in AGC2, the adaptive digital gain is never negative and is capped so background noise stays "
               "under -50 dBFS; attenuation of hot inputs is left to the limiter and the mic-gain "
               "recommendation.",
         verification="The first e2e expectations contradicted the AGC2 design (noise cap, no attenuation) and "
                      "were rewritten to test those behaviours. A unit test reused a buffer that the limiter had "
                      "already modified in place."),
    dict(id="6.5", slug="high-pass-filter", title="High-pass filter",
         problems=["Low-frequency rumble or DC offset -> high-pass filter"],
         headers=["audio/high_pass_filter.h"], unit=["HighPassFilter"], e2e=["E2E_HighPassFilter"],
         bench=["audio/HighPassFilter_10ms"],
         scenario="A capture with 0.1 DC offset and 50 Hz hum through HPF -> NS -> AGC; the DC and hum left at "
                  "the output are measured.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. ", verification="No defects found; the frequency response matches 4th-order Butterworth."),
    dict(id="6.6", slug="transient-suppressor", title="Transient (keyboard click) suppression",
         problems=["Keyboard typing noise -> transient suppressor"],
         headers=["audio/transient_suppressor.h"], unit=["TransientSuppressor"], e2e=["E2E_TransientSuppressor"],
         bench=["audio/TransientSuppressor_10ms"],
         scenario="Speech with a keyboard click every 250 ms; the click energy that falls in speech pauses is "
                  "compared with and without suppression.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. Uses Vad::voiced() (this frame's own spectrum, no hangover) to avoid attacking vowels.",
         verification="Three defects were found and fixed. (1) The envelope started at 1e-8 and was frozen during "
                      "transients, so it locked into 'transient' forever. (2) The gain ramped down across the "
                      "first millisecond, which holds most of a click's energy; attack is now instant. (3) A "
                      "60 ms envelope still held the speech level right after a word, so a 4 ms envelope (onset "
                      "detection) is used. Attenuation in pauses went from 3 dB to 13.5 dB."),
    dict(id="6.7", slug="dtx-cng", title="DTX and comfort noise (RFC 3389)",
         problems=["Silence wasting bandwidth -> VAD + DTX (discontinuous transmission)",
                   "Silence sounding 'dead' at the receiver -> CNG (comfort noise generation)"],
         headers=["audio/comfort_noise.h"], unit=["ComfortNoise"], e2e=["E2E_ComfortNoise"],
         bench=["audio/DtxController_10ms", "audio/ComfortNoise_10ms"],
         scenario="A one-sided stretch of a call (talks 15 s, listens 15 s) at -50 dBFS background noise; "
                  "speech frames and SIDs cross a 2%-loss link. Transmitted-frame ratio against the actual "
                  "speech ratio, and comfort-noise level against the real noise in pauses.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. SID reflection coefficients are interpolated between updates, so the synthesis filter is always "
               "stable.",
         verification="Defect found: SIDs were sent too often, because spectral-change detection used "
                      "single-frame reflection coefficients (sigma about 0.08). The background description now "
                      "comes from an averaged autocorrelation, and the transmit ratio fell from 0.65 to 0.49 "
                      "(speech 0.43). A measurement bug that counted lost speech packets as silence was also "
                      "fixed."),
    dict(id="6.8", slug="vad", title="Voice activity detection",
         problems=["Silence wasting bandwidth -> VAD", "Detecting who's talking -> voice activity"],
         headers=["audio/vad.h"], unit=["Vad"], e2e=["E2E_Vad"],
         bench=["audio/Vad_10ms"],
         scenario="Speech in noise at about 10 dB SNR after noise suppression; recall and false-positive rate "
                  "(beyond the intended 200 ms hangover).",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. ", verification="Defects found: (1) residual 'musical noise' after NS looked voiced in a single "
                                "frame (false-positive rate 27%), so flatness now uses a ~30 ms averaged spectrum "
                                "(9%); (2) after a +20 dB noise step the floor rose only 3 dB/s, so noise-like "
                                "frames now raise it at 30 dB/s. The unit-test metric was also changed to "
                                "exclude the intended hangover."),
    dict(id="6.9", slug="audio-level", title="Audio level (RFC 6464)",
         problems=["Showing who is talking, and voice activity without decoding -> audio level header extension (RFC 6464)"],
         headers=["audio/audio_level.h"], unit=["AudioLevel"], e2e=["E2E_AudioLevel"],
         bench=["audio/AudioLevel_20ms"],
         scenario="Level and voice flag are encoded into the RFC 6464 byte and decoded again; the level of known "
                  "tones and silence must match.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. AudioLevelMeter is libwebrtc's voe::AudioLevel (issue #101): the stats level of a send stream, the maximum absolute sample refreshed every 11th frame and decayed to a quarter, and total energy and duration. ComputeLevel stays the RFC 6464 header level (RMS in -dBov). ", verification="Active speaker detection was removed with the other SFU-only components (issue #27); "
                      "the RFC 6464 codec and level are unchanged."),
    dict(id="6.11", slug="resampler", title="Sample-rate conversion",
         problems=["Device sample-rate mismatch -> resampler"],
         headers=["audio/resampler.h"], unit=["Resampler"], e2e=["E2E_Resampler"],
         bench=["audio/Resampler_48k_to_16k_10ms", "audio/Resampler_44k1_to_48k_10ms"],
         scenario="44.1 kHz microphone -> 16 kHz processing (HPF) -> 48 kHz speaker with a 1 kHz tone; "
                  "end-to-end SNR after fitting gain and phase, plus the output length.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The Audio Device Module (platform audio I/O, device switching) is platform glue rather than an "
               "algorithm and is out of scope; the resampler is what bridges device and processing rates.",
         verification="Defect found: when decimating, the filter used too few taps (transition band several kHz "
                      "wide), so a 10 kHz tone aliased at only -36 dB into 48 -> 16 kHz output. Taps now scale "
                      "with the decimation factor: 91 dB rejection, 107 dB SNR."),
    # ------------------------------------------------------------------ 7
    dict(id="7.1", slug="freeze-detector", title="Freeze detection",
         problems=["Lost frame (decode impossible) -> freeze the last frame until recovery (measured as freezes)"],
         headers=["video/freeze_detector.h"], unit=["FreezeDetector"], e2e=["E2E_FreezeDetector"],
         bench=["video/FreezeDetector_frame"],
         scenario="60 s of 30 fps video with 1% frame loss, two 600 ms outages and PLI-driven recovery; the "
                  "detector's freeze count is compared with gaps counted directly from the render log.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. Keyframe requests that end freezes are in 3.5.",
         verification="A unit-test expectation was wrong about the harmonic frame rate: irregular 38/38/23 ms "
                      "intervals give 28.97 fps, not 30.3, which is the point of the metric."),
    dict(id="7.2", slug="codec-fallback", title="Hardware -> software codec fallback",
         problems=["Decoder errors or corrupt bitstream -> decoder fallback (hardware -> software)",
                   "Hardware encoder failure -> automatic fallback to a software encoder"],
         headers=["video/codec_fallback.h"], unit=["CodecFallback"], e2e=["E2E_CodecFallback"],
         bench=["video/CodecFallback_frame"],
         scenario="A hardware encoder that starts failing 30% of frames at 10 s; checks that exactly one "
                  "fallback happens with one keyframe, and that the longest gap between encoded frames stays "
                  "small.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. ", verification="No defects found."),
    dict(id="7.3", slug="orientation", title="Video orientation (CVO)",
         problems=["Rotated camera -> video orientation header extension (CVO)"],
         headers=["video/orientation.h"], unit=["Orientation"], e2e=["E2E_Orientation"],
         bench=["video/Orientation_rotate90_720p"],
         scenario="A phone rotating through all 4 rotations, with and without mirroring; frames carry CVO in a "
                  "real RTP header extension and the receiver must show the scene upright, bit-exact.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. ", verification="No defects found."),
    dict(id="7.4", slug="content-policy", title="Content hints and screen-content variable frame rate",
         problems=["Screen sharing (text must stay sharp) -> content hint, screen-content coding tools, variable framerate",
                   "Talking head vs. motion -> content-type-aware rate control"],
         headers=["video/content_policy.h"], unit=["ContentPolicy"], e2e=["E2E_ContentPolicy"],
         bench=["video/FrameChangeDetector_1080p", "video/Q_ScreenShare_encode_ratio"],
         scenario="60 s of screen sharing at 15 fps: a new slide every 10 s and a blinking cursor; every content "
                  "change must be encoded immediately, while static frames are skipped.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. Screen-content coding tools (AV1 palette/intra-BC, HEVC SCC) are encoder features; the policy "
               "only flags that they should be enabled.",
         verification="No defects found."),
    dict(id="7.5", slug="codec-negotiation", title="Codec negotiation",
         problems=["Many codecs and devices -> codec negotiation (VP8, VP9, H.264, AV1, H.265 recently)"],
         headers=["video/codec_negotiation.h"], unit=["CodecNegotiation"], e2e=["E2E_CodecNegotiation"],
         bench=["video/CodecNegotiation_offer10"],
         scenario="A browser-like offer (VP8, VP9, three H.264 profiles, AV1, RTX) against an embedded camera "
                  "(hardware H.264 CB level 4.0, software VP8, no transport-cc), in both directions.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. ", verification="No defects found."),
    dict(id="7.6", slug="frame-scaler", title="Resolution scaling and aspect handling",
         problems=["Frame size and aspect changes -> resolution adaptation and scaling mid-stream"],
         headers=["video/frame_scaler.h"], unit=["FrameScaler"], e2e=["E2E_FrameScaler"],
         bench=["video/Scale_720p_to_360p", "video/Scale_360p_to_720p", "video/Q_Scaler_psnr_720p_via_360p"],
         scenario="720p frames down-scaled along the adaptation ladder and up-scaled back for display (PSNR per "
                  "step), plus a 4:3 camera shown in a 16:9 tile with crop and letterbox.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. Portable scalar code, about 1.7 ms per 720p frame on the reference machine; a SIMD library "
               "(libyuv) is the natural drop-in for production.",
         verification="No defects found."),
    dict(id="7.7", slug="frame-scheduler", title="Render timing and late-frame dropping",
         problems=["Frames arriving too late to render -> render timing and late-frame dropping"],
         headers=["video/frame_scheduler.h"], unit=["FrameScheduler"], e2e=["E2E_FrameScheduler"],
         bench=["video/FrameScheduler_frame"],
         scenario="30 fps over 15 ms jitter: VideoJitterEstimator + VideoTiming give render times and the "
                  "scheduler presents frames. Render-interval jitter is compared with rendering on arrival.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. ", verification="Exposed the 4.6 jitter-offset defect (render-interval jitter 7.9 ms before the "
                                "fix, 1.1 ms after)."),
    dict(id="7.8", slug="quality-scaler", title="QP-based quality scaler",
         problems=["Region-of-interest quality -> quality scaler and per-codec QP control"],
         headers=["video/quality_scaler.h"], unit=["QualityScaler"], e2e=["E2E_QualityScaler"],
         bench=["video/QualityScaler_frame"],
         scenario="An H.264-like encoder model (QP +6 per halving of bits per pixel) with bandwidth 2500 -> 300 "
                  "-> 2500 kbps; resolution follows the QP signals.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. Per-region QP (ROI) is an encoder-side feature built on top of these thresholds.",
         verification="An expectation was wrong: at 300 kbps, 640x360 already gives QP 33, which is inside the "
                      "H.264 band, so not going lower is correct. The test now checks that QP returns inside the "
                      "band."),
    # ------------------------------------------------------------------ 8
    dict(id="8.1", slug="rtp-to-ntp", title="RTP -> NTP mapping",
         problems=["Lip-sync (A/V sync) -> RTCP Sender Reports map the RTP timestamp to NTP wallclock"],
         headers=["sync/rtp_to_ntp.h"], unit=["RtpToNtp"], e2e=["E2E_RtpToNtp"],
         bench=["sync/RtpToNtp_UpdateAndEstimate", "sync/RtpToNtp_Estimate"],
         scenario="120 s with a sender audio clock 80 ppm off its NTP clock; SRs every second over a jittery "
                  "link. Every 20 ms the estimated capture time of the current RTP timestamp is compared with "
                  "the truth.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. ", verification="No algorithm defects; one unit test's reject counting was corrected."),
    dict(id="8.2", slug="stream-sync", title="Audio/video synchronization and CNAME sync groups",
         problems=["Lip-sync (A/V sync) -> the receiver aligns playout delay between streams",
                   "Synchronizing multiple streams from one sender -> shared CNAME, MediaStream grouping"],
         headers=["sync/stream_sync.h"], unit=["StreamSync"], e2e=["E2E_StreamSync"],
         bench=["sync/StreamSync_Update"],
         scenario="Audio (30 ms network, 60 ms buffer) and video (90 ms network, 40 ms decode/render) from one "
                  "CNAME; at 40 s the video path gains 120 ms. Measures the true A/V offset before sync, after "
                  "settling and after the route change.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. The 30 ms dead band sits inside the ITU-R BT.1359 detectability window (+45/-125 ms).",
         verification="The test's 'before' sample was taken after the first corrections; it is now taken at t=0 "
                      "(40 ms offset)."),
    dict(id="8.3", slug="capture-time", title="Absolute capture time and glass-to-glass latency",
         problems=["Capture-to-render latency measurement -> absolute capture time extension, timing extensions"],
         headers=["sync/capture_time.h"], unit=["CaptureTime"], e2e=["E2E_CaptureTime"],
         bench=["sync/AbsCaptureTime_SenderAndInterpolator"],
         scenario="A sender 2.5 s ahead of the receiver; an SFU rewrites RTP timestamps but forwards "
                  "abs-capture-time; the receiver learns the clock offset from SRs and measures capture-to-render "
                  "latency for every frame.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. ", verification="Portability finding: interpolation used long double, which is software-emulated "
                                "on WebAssembly and made the benchmark miss its budget there (347 ns vs 200). "
                                "It now uses exact integer arithmetic."),
    dict(id="8.4", slug="header-extensions", title="Header extensions (RFC 8285) and BUNDLE demuxing",
         problems=["Identifying streams -> SSRC, MID and RID header extensions"],
         headers=["sync/header_extensions.h"], unit=["HeaderExtensions"], e2e=["E2E_HeaderExtensions"],
         bench=["sync/HeaderExtensions_WriteParse4", "sync/RtpDemuxer_by_ssrc"],
         scenario="One 5-tuple carrying audio, three simulcast video encodings and RTX (repaired RID) with "
                  "reordering; MID/RID are only on each SSRC's first 10 packets, and one stream changes SSRC "
                  "mid-call. Routing accuracy.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. ", verification="No defects found."),
    # ------------------------------------------------------------------ 9
    dict(id="9.1", slug="rtcp-engine", title="RTCP engine, SDES and BYE",
         problems=["Receiver and sender reports, RTT and feedback -> one RTCP engine per session",
                   "Early feedback (NACK, PLI, FIR, loss notification, transport-cc, REMB) -> compound packets sent at once"],
         headers=["rtp/rtcp_engine.h", "rtp/rtcp_sdes_bye.h"], unit=["RtcpEngine", "RtcpSdesBye"], e2e=["E2E_RtpSession"],
         bench=[],
         scenario="Two sessions exchange bytes over sim::NetworkLink (tests/e2e/rtp_session_e2e_test.cc): reports "
                  "flow in both directions at the scheduled intervals, RTT is measured from SR and RR, and feedback "
                  "requests reach the module that acts on them.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Follows libwebrtc's RTCPSender and RTCPReceiver for the report interval (1 s with video, 5 s "
               "without, randomization 0.5 to 1.5x, first report at half the interval) and for the compound layout "
               "(SR or RR first, SDES CNAME after it). Version 2 (`rtcp v2`, issue #76 group 3) times reports as "
               "RTCPSender::ComputeTimeUntilNextReport: whole milliseconds drawn from [0.5, 1.5] x the minimum, at least "
               "1 ms, and min(360 s / kbit/s, interval) for a video sender above 72 kbit/s; the rate is taken from the SR octet counter (media payload), lower than libwebrtc's all-packet egress rate (remaining work, #76). With `rtcp v2` a report due within 100 ms is sent ahead of a video keyframe (RTCPSender::TimeToSendRTCPReport, issue #101). Differs in having one engine per session where libwebrtc has "
               "one per SSRC, which one-to-one calls allow. Wire layouts are checked against libwebrtc's parsers in "
               "compare/webrtc/rtcp_interop.cc (CI job compare-webrtc).",
         verification="Introduced by #32 plan PRs 1 and 2. The Owner checked the scheduling constants against "
                      "libwebrtc M157 (rtcp_sender.cc, video_send_stream.h, audio_send_stream.h), see the issue. "
                      "Per-suite results are recorded by ctest and CI; tools/run_all.sh does not run these suites yet."),
    dict(id="9.2", slug="rtp-sender", title="RTP sender and send-side controller",
         problems=["Packetization, sequence numbers, RTX history and RED -> RtpSender",
                   "Pacing, transport-wide sequence numbers and congestion control hand-off -> SendSideController"],
         headers=["rtp/rtp_sender.h", "rtp/send_side_controller.h"], unit=["RtpSender", "SendSideController"],
         e2e=["E2E_RtpSender"], bench=[],
         scenario="Frames go through RtpSender and SendSideController over sim::NetworkLink; the test checks the "
                  "packet bytes, retransmission on NACK, pacing and that the transport-cc feedback drives the "
                  "target rate.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. RtpSender builds packets and never sends; every packet leaves through TakePackets() with a pacer "
               "priority. SendSideController (one per transport) numbers, paces and serializes them. Padding and "
               "probes are real padding-only RTP packets. RTX of RED is retransmitted as sent, as in libwebrtc's "
               "RtxReceiveStream. Packet size is capped at 1200 bytes, libwebrtc's kVideoMtu. Interop for RTP with "
               "MID, transport-cc and abs-send-time, RTX and padding is in compare/webrtc/rtcp_interop.cc. The encoder "
               "budget (SendSideController::ComputeMeasuredRates) takes off the share of the last second's "
               "non-audio bytes that was RTX or FEC, capped at 50%, exactly as libwebrtc's FecControllerDefault::"
               "UpdateFecRates (compare/webrtc/reference/modules/video_coding/fec_controller_default.cc). There is no "
               "retransmission rate cap, as in M157. Differences that remain: the FEC amount (#7, "
               "FecControllerDefault's media_opt_util choice) and keyframe requests (#23); the encoder floor of 30 kbps "
               "applies even when the audio alone exceeds the target (#37).",
         verification="Introduced by #32 plan PRs 3 and 5. Issue #21 (partial) added the measured repair share; "
                      "see docs/fixes/21-repair-within-target.md. Review notes from PR 3 (padding timestamp, padding size "
                      "accounting, zero-byte frames) were fixed in PR 5. Per-suite results are recorded by ctest "
                      "and CI; tools/run_all.sh does not run these suites yet."),
    dict(id="9.3", slug="rtp-receiver", title="RTP receiver",
         problems=["Parsing, demultiplexing and repair on the receive path -> RtpReceiver",
                   "Frame assembly, keyframe requests and playout scheduling -> RtpReceiver"],
         headers=["rtp/rtp_receiver.h"], unit=["RtpReceiver"], e2e=["E2E_RtpReceiver"], bench=[],
         scenario="Video and audio streams over lossy, reordering links: RTX, FEC and NACK recover packets, the "
                  "packet buffer releases decodable frames, and audio is unwrapped from RED into the jitter buffer.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Frame ids and dependencies come from sequence numbers rather than a descriptor (design 6.4). LNTF is "
               "sent only when negotiated and frame information is present, so it is off for H.264, as in "
               "libwebrtc (follow-up #47).",
         verification="Introduced by #32 plan PRs 4 and 6. Per-suite results are recorded by ctest and CI; "
                      "tools/run_all.sh does not run these suites yet."),
    dict(id="9.4", slug="session", title="RTP session facade",
         problems=["Wiring the building blocks by hand -> one rtp::Session per endpoint of a one-to-one call"],
         headers=["rtp/session.h"], unit=["Session"], e2e=["E2E_RtpSession"], bench=[],
         scenario="The test bench runs a full call through two rtp::Session objects, and every packet crosses "
                  "sim::NetworkLink as bytes. The whole scenario suite is the end-to-end test.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Pull-based: every output is a Take*() call, so the order of effects is fixed. The app keeps the "
               "codecs, the frame dropper and the quality scaler, and reads the rates the session publishes.",
         verification="Introduced by #32 plan PRs 5 and 6. Switching the test bench changed baseline metrics only "
                      "through real packet sizes; each change is explained in PR #46. Per-suite results are "
                      "recorded by ctest and CI; tools/run_all.sh does not run these suites yet."),
    dict(id="9.5", slug="h264", title="H.264 packetization (RFC 6184) and fake encoder",
         problems=["Video payload format on the wire -> H.264 packetization-mode 1: single NAL, STAP-A, FU-A"],
         headers=["rtp/h264.h", "rtp/fake_h264.h"], unit=["H264"], e2e=["E2E_RtpSession"], bench=[],
         scenario="The test bench's encoder model emits SPS, PPS and IDR on keyframes and one slice on delta "
                  "frames; the packetizer splits them, the depacketizer rebuilds the access unit.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Follows libwebrtc's RtpPacketizerH264 and VideoRtpDepacketizerH264 (checked by the interop test "
               "against H26xPacketBuffer). The fake encoder follows FakeH264Encoder and differs in making a keyframe "
               "when asked and never writing a zero byte into the filler. An IDR alone counts as a keyframe, as in "
               "libwebrtc by default.",
         verification="Introduced by #32 plan PR 7. The 24-byte SPS+PPS packet that now opens the call exposed a "
                      "start-up fragility in the pacer and probing code (first probe cluster rejected), tracked in "
                      "#48. Per-suite results are recorded by ctest and CI; tools/run_all.sh does not run these "
                      "suites yet."),
]

# Problem rows from sections 3-8 that are platform glue, codec internals or
# covered as part of another algorithm. Listed in the coverage table.
NOT_IMPLEMENTED = [
    ("3", "Opus in-band FEC (LBRR), Opus DRED", "Inside the Opus codec; used by configuring the codec. 3.4 is the codec-agnostic fallback."),
    ("3", "Keyframe pacing, intra refresh, LTR frames", "Encoder features (codec-specific bitstream tools)."),
    ("6", "Audio Device Module (platform I/O, mute, device switching)", "Platform glue per OS; the algorithms take 10 ms PCM frames. Rate bridging is 6.11."),
    ("7", "Error resilience inside the decoder", "Codec internal; 7.2 handles the fallback decision."),
    ("7", "Screen-content coding tools (palette, intra-BC)", "Encoder features; 7.4 flags when to enable them."),
]
