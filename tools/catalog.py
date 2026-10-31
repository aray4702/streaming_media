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
               "A request is repeated once one RTT has passed since it was sent, with no floor below the RTT (`GetNackBatch`'s "
               "`nack_on_rtt_passed`; the 20 ms `min_retry_interval` was removed in issue #846); the sender also refuses to resend the same packet twice within one RTT, which protects "
               "against duplicate NACKs. A packet recovered (restored from RTX or by FEC) ahead of the newest received one goes to "
               "NackRequester's `recovered_list_`: it does not move the newest, and the gap revealed by the next packet received as "
               "sent skips it, so it is never NACKed (issue #846). The request is the standard generic "
               "NACK (RFC 4585 6.2.1, PID+BLP items) from loss/rtcp_feedback.h, byte-compatible with libwebrtc's "
               "rtcp_packet::Nack (interop check in compare/webrtc/rtcp_interop.cc). "
               "The RTX history is libwebrtc's RtpPacketHistory (issue #76 group 2; the v1 ring was deleted in #167): a packet enters when "
               "the pacer sends it, stays max(3 x RTT, 1 s) and is culled after three times that (or when 600 are held "
               "and the oldest is past the retention, at most 9600), leaves when transport feedback acknowledges it, and a "
               "request is ignored while a resend is queued or within one RTT of the last send. A queued resend that a keyframe flush discards stays pending, so later requests for it are ignored, as libwebrtc's flush leaves its history entry (issue #151). Without FEC the RTP sequence number is "
               "assigned when the pacer releases the packet (`RtpSender::AssignSequenceNumber`, as `PacketRouter` / `AssignSequenceNumber` do), so a packet a "
               "keyframe flush discards leaves no gap (issues #127, #154, [fix record](../../fixes/154-sequence-at-pacer-release.md)). "
               "A video packet that transport-wide feedback reports lost, with no later packet of its SSRC received in that feedback, is resent "
               "at once through the same history checks (RtpVideoSender::OnPacketFeedbackVector's early loss detection, call/rtp_video_sender.cc, "
               "issue #217), so the NACK that follows finds it pending or sent within an RTT. Remaining difference: "
               "retransmitted-flush settings are off as in the libwebrtc default. With `pacer v2` the history also gives payload "
               "padding (`GetPayloadPaddingPacket` in `PaddingMode::kRecentLargePacket`: the most recent large payload, "
               "kept after it has left the history), see docs/fixes/120-rtx-payload-padding.md. The NackTracker is libwebrtc's NackRequester (issue #23; the v1 policy was deleted in #167): "
               "it retries on libwebrtc's 20 ms process tick, gives up with the kMaxNackRetries-th (100th) request, which erases the entry as it is sent, "
               "without a keyframe request, and a list that would pass 1000 "
               "entries is cleared and asks for a keyframe. The first request for a gap leaves with the packet that revealed it "
               "(`NackRequester::OnReceivedPacket`'s `GetNackBatch(kSeqNumOnly)`, sent before the packet buffer runs), so a packet "
               "reordered within the same millisecond is NACKed too, as in libwebrtc (issue #411, `issue_411_nack_on_reorder`). "
               "There is no reorder wait: M157's `NackRequester::OnReceivedPacket` sets `is_retransmitted = true` for every packet, "
               "so its reordering histogram stays empty and `WaitNumberOfPackets(0.5)` is 0 (issue #867).",
         verification="No defects found. Measured overhead (~5.4%) tracks the loss rate, as expected for "
                      "selective retransmission. `issue_151_outage_3s_v2` holds the freeze total at < 3300 ms "
                      "(libwebrtc 3221 ms on seed 1) since #151 turned the outage monitor off under `cc v2`. "
                      "Issue #518: the 20 ms retry ticks keep the grid of the first, as `NackPeriodicProcessor`'s "
                      "RepeatingTask (`repeating_task.cc:87`), when the receiver runs between them, and `NextProcessTime` "
                      "reports the next tick (unit `NackTracker.RetryTicksKeepTheGridOfTheFirstAndReportTheNext`). "
                      "Issue #846: `recovered_list_` and no retry floor, as `nack_requester.cc` (units "
                      "`NackTracker.RecoveredPacketAheadIsNeverNacked`, `RecoveredPacketBehindLeavesTheList`, "
                      "`RecoveredListIsClearedAndAged`, `RetryHasNoFloorBelowTheRtt`; libwebrtc's `nack_requester_unittest.cc` is not in `reference/`). "
                      "Issue #867: the 100th request erases its entry, and reordering never delays a first request (units "
                      "`NackTracker.HundredthRequestErasesTheEntryAsItIsSent`, `ReorderingNeverDelaysTheFirstRequest`); "
                      "`issue_867_nack_reorder_loss` (3% loss, 60 ms jitter with reordering) is compared with libwebrtc on 40 seeds. "
                      "Its freezes each follow a video packet whose first retransmission was lost too."),
    dict(id="3.2", slug="fec", title="Forward error correction (ULPFEC over RED, as libwebrtc)",
         problems=["Loss where a retransmit would arrive too late (high RTT) -> FEC: ULPFEC, FlexFEC"],
         headers=["loss/ulpfec.h"], unit=["Ulpfec"], e2e=["E2E_Fec"],
         bench=["loss/FecEncode_10pkt_frame_50pct", "loss/FecDecode_RecoverOne", "loss/Q_Fec_5pct_rtt300"],
         scenario="Same video stream at 150 ms one-way delay (NACK disabled), media in RED and ULPFEC on the media "
                  "SSRC: random 5% loss at 50% protection with random masks, and Gilbert-Elliott bursty loss "
                  "comparing libwebrtc's bursty masks against its random ones. Frames must be complete within the "
                  "250 ms budget.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "ULPFEC (RFC 5109) over RED (RFC 2198), the only FEC format (Owner, 2026-10-15), ported from libwebrtc M157 "
               "(`forward_error_correction.cc`, `forward_error_correction_internal.cc`, `ulpfec_header_reader_writer.cc`, "
               "`ulpfec_generator.cc`, `ulpfec_receiver.cc`): `loss/ulpfec.h`, with libwebrtc's random and bursty "
               "packet mask tables (read from the pinned prebuilt libwebrtc, as `reference/` has only their headers), "
               "interleaved masks above 12 packets, Q8 protection factors, kMinMediaPackets 4 above protection 80 and "
               "kMaxExcessOverhead 50. "
               "Since #29's second step the session layer sends and receives it as libwebrtc does: `RtpSender` wraps every "
               "video packet in RED when FEC is on (`rtp_sender_video.cc` BuildRedPayload), feeds the packet as the pacer "
               "sends it, without RED, to the `UlpfecGenerator` (`RtpSenderEgress::SendPacket`), and the FEC packets go "
               "back to the pacer (`PacketRouter::FetchFec`) at FEC priority, on the media SSRC, numbered when sent. The "
               "payload room of each packet is reduced by `RTPSenderVideo::FecPacketOverhead`. A RED packet is resent "
               "on RTX with the RTX payload type associated with RED (`rtx_payload_type_map_`; 118/119/99 as Chrome "
               "offers them). `RtpReceiver` hands RED packets to the `UlpfecReceiver` and treats an FEC packet's "
               "sequence number as received (`RtpVideoStreamReceiver2::ParseAndHandleEncapsulatingHeader`, "
               "`NotifyReceiverOfEmptyPacket`), so it is not NACKed. The library's own XOR format (`loss/fec.h`, a "
               "separate FEC SSRC) is removed. "
               "The FEC a packet completes joins the queue the pacer's send loop is draining, as libwebrtc's "
               "`ProcessPackets` does after each `SendPacket` (#771). "
               "The RTX payload types are `ConfigureRtx`'s map (`call/rtp_video_sender.cc`): the stream's payload "
               "type and, whenever RED is configured with an RTX type, RED's, also while FEC is off beside NACK; "
               "RTX padding takes the RTX type of the lowest associated one (`rtx_payload_type_map_.begin()`, "
               "`RTPSender::BuildPaddingPacket`). The sender report's packet and octet counts are libwebrtc's "
               "`transmitted` counters of the media and RTX SSRCs: every packet as it is sent, FEC, retransmissions "
               "and padding included (`RtpSenderEgress::UpdateRtpStats`, #816). The FEC amount is "
               "`FecControllerDefault` (3.7).",
         verification="ULPFEC (#29, first PR): unit tests check the RFC 5109 byte layout, the L bit, recovery of every "
                      "RTP header field across the sequence-number wrap, zero columns for sequence gaps, malformed input, "
                      "the generator's block rules and RED wrapping, and a generator-to-receiver run with 5% loss. The "
                      "bursty tables recover every run of up to m consecutive losses for all (k, m) up to 12, as "
                      "`fec_private_tables_bursty.h` states; the test fails if the random table is used in their place. "
                      "Session layer (#29, second PR): `RtpSender.UlpfecOverRedOnTheMediaSsrc` (RED wrapping, FEC after "
                      "the frame's last packet is sent, on the media SSRC with the next sequence numbers, not in the "
                      "history), `RtpSender.RedPacketIsResentWithTheRedRtxPayloadType`, `RtpSender.FecPacketOverhead`, "
                      "`RtpReceiver.UlpfecRecoversALostMediaPacket` (recovered, and no NACK for the FEC or the recovered "
                      "sequence numbers) and `RtpReceiver.RedPacketRestoredFromRtx`. In the e2e simulation 50% random-mask "
                      "protection keeps 99.0% of frames on time at 5% loss (67.4% without); on Gilbert-Elliott bursts the "
                      "bursty masks recover 150 packets against 143 for the random ones. No stage-1 scenario sends video "
                      "FEC (H.264 with NACK, `ShouldDisableRedAndUlpfec`), so the suite's metrics are unchanged. "
                      "#816: `PacingController.FecFromThePacketSenderLeavesInTheSameProcessCall`, "
                      "`RtpSender.RtxPaddingTakesTheRtxTypeOfTheLowestAssociatedPayloadType` and "
                      "`RtpSender.SenderReportCountsEveryPacketSentOnTheMediaAndRtxSsrcs` fail before the port and pass "
                      "after it. "
                      "Interop (#29, last PR): `compare/webrtc/rtcp_interop.cc` (CI job compare-webrtc) has libwebrtc's "
                      "`UlpfecReceiver` recover, byte for byte, the packets this library's `UlpfecGenerator` protected, "
                      "and this library's `UlpfecReceiver` recover those libwebrtc's `UlpfecGenerator` protected, on an "
                      "8-packet block (16-bit mask, 3 lost) and a 20-packet block (48-bit mask, interleaved masks, "
                      "across the sequence-number wrap, 2 lost) with CSRCs, extensions and padding; the two generators "
                      "also make the same FEC payloads. On `random_loss_5pct`, `burst_loss` and `satellite`, seeds 1-5, "
                      "neither stack sends video FEC (0 kbps on both), so recovery per parity packet and "
                      "`video.residual_loss_pct` are unchanged from before the port."),
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
         verification="No defects found; results match the analytical p^2 and p^3. The test bench's Opus send path (issue #126) "
                      "feeds it libopus's frames: since #457 the bench encodes its speech with libopus 55513e81, the "
                      "revision libwebrtc M157 builds, set up as `AudioEncoderOpusImpl` sets it up (`testbench/opus_encoder.h`: "
                      "48 kHz mono VOIP, 20 ms, 32 kbps VBR, complexity 9, DTX off, in-band FEC on, max bandwidth fullband). "
                      "It replaced the frame-size model fitted to libwebrtc's traces (#126, #375), which was an approved deviation "
                      "(Owner's decision C on #452). Receive side (#722): with `neteq v2` the receiver splits RED as NetEq's "
                      "`RedPayloadSplitter::SplitRed` and `CheckRedPayloads` (`SplitRed`), primary first, then the redundant "
                      "blocks newest to oldest, every block to the jitter buffer; on `issue_533_opus_decoder_frame_older_than_window` "
                      "seed 1 the DelayManager's 923 updates (arrival delay, reordered flag, target) are then the same as "
                      "libwebrtc's (yardstick #779), where the header-order, deduplicated split differed from the first one."),
    dict(id="3.4", slug="plc", title="Packet loss concealment (PCM domain)",
         problems=["Lost audio frames -> PLC in NetEq (Opus in-band FEC / DRED are codec features, see notes)"],
         headers=["timing/neteq_expand.h"], unit=["NetEqExpand", "NetEqMerge", "NetEqBackgroundNoise", "NetEqRandomVector"],
         e2e=["E2E_Plc"],
         bench=["timing/Q_Plc_10pct_speech"],
         scenario="20 s of synthetic voiced speech in 20 ms packets over a link with 10% random loss and a constant "
                  "40 ms delay, played 10 ms at a time through the audio jitter buffer (4.5) with time stretch off. "
                  "Every lost packet must be concealed, and the concealed audio must keep the speech's level "
                  "(RMS of the expand blocks against the normal blocks).",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "Concealment is libwebrtc M157's NetEq: `Expand` (with `RandomVector`), `BackgroundNoise`, `Merge` "
               "and `Normal`'s first decode after an expand (modules/audio_coding/neteq/expand.cc, "
               "background_noise.cc, random_vector.cc, merge.cc, normal.cc), ported as `NetEqExpand`, "
               "`NetEqBackgroundNoise` and `NetEqMerge` (`timing/neteq_expand.h`, #62) and bit-exact with them on "
               "int16 input (#789, #796). The audio jitter buffer (4.5) runs them; where its sync buffer can't "
               "fill the output it plays silence as NetEq's `kSampleUnderrun` (#568). Opus LBRR (in-band FEC) and "
               "DRED live inside the Opus codec; in-band FEC goes into the buffer as NetEq's ParsePayload puts it "
               "(see 4.5). The library's own codec-agnostic concealer, `loss::PacketLossConcealer` (pitch repeat "
               "with its own fades, the jitter buffer's version 1), was not libwebrtc's and was removed with "
               "version 1 in #817.",
         verification="NetEq's Expand, Merge and BackgroundNoise unit tests (golden hashes from the reference "
                      "sources compiled unchanged) pass. #568 removed PacketLossConcealer from version 2, and "
                      "#817 removed it and version 1. `E2E_Plc` and `Q_Plc_10pct_speech` measure NetEq's "
                      "concealment (they measured PacketLossConcealer's before #817): seed 11, 11.2% packet loss, "
                      "11.2% of the samples concealed in 98 expands, the expand blocks at 0.90 of the normal "
                      "blocks' level."),
    dict(id="3.5", slug="keyframe-requests", title="Keyframe requests (PLI/FIR) and loss notification",
         problems=["Lost or undecodable video frame -> PLI / FIR keyframe request; LNTF (loss notification)",
                   "Lost frame (decode impossible) -> keyframe request; freeze the last frame until recovery"],
         headers=["loss/keyframe_request.h", "loss/rtcp_feedback.h"], unit=["KeyframeRequest", "RtcpFeedback"], e2e=["E2E_Keyframe"],
         bench=["loss/KeyframeRequester_Frame", "loss/Q_Keyframe_2pct_frame_loss"],
         scenario="30 s of 30 fps video with an IPPP reference chain and 2% unrecoverable frame loss "
                  "(after NACK). The sender answers each PLI with a keyframe. Measures the decodable-frame ratio, "
                  "the longest freeze and the number of PLIs (no request storms).",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "The receiver asks on libwebrtc's RtpVideoStreamReceiver2 triggers (issue #23): the first assembled frame is "
               "not a keyframe, a decoder error, a NACK list or packet buffer overflow, an H.264 IDR without its "
               "parameter sets, and the decode time-out "
               "(200 ms before the first frame, 3 s after; not while a keyframe is arriving or the stream is idle). "
               "For codecs other than H.264, a sequence-number discontinuity (a media packet more than 2048 behind "
               "the newest with a newer RTP timestamp) drops packets and asks again every 200 ms until a newer "
               "keyframe re-anchors the receiver (UnwrapSequenceNumberOrRecover, issue #818). The missing-video-"
               "structure trigger needs the dependency descriptor extension, which the library does not carry, so it "
               "cannot fire. Every request is the configured method, PLI by default as libwebrtc's "
               "`rtp.keyframe_method` (KeyframeRequestConfig::method; FIR sequence numbers start at 1, issue #818). "
               "A keyframe is arriving while a packet came within 200 ms whose depacketized frame type is key (every "
               "FU-A fragment of an IDR slice, an SPS or IDR NAL unit) or that has the last keyframe's RTP timestamp, "
               "counting packets received as sent only (RtpVideoStreamReceiver2::UpdatePacketReceiveTimestamps, "
               "issue #335). "
               "A blocked frame is left to NACK: there is no repair window, retry timer or receiver throttle (the v1 "
               "policy that had them was deleted in #167). Wire formats (loss/"
               "rtcp_feedback.h): PLI (RFC 4585 6.3.1), FIR (RFC 5104 4.3.1, with its own request sequence number), "
               "loss notification (LNTF, libwebrtc's goog-lntf application-layer feedback) and compound RTCP with "
               "the report first; each is checked against libwebrtc's parsers in compare/webrtc/rtcp_interop.cc. "
               "On the sender, the test bench runs libwebrtc's EncoderRtcpFeedback::OnReceivedIntraFrameRequest "
               "(video/encoder_rtcp_feedback.cc): a PLI or FIR within kMinKeyframeSendIntervalMs (300 ms) of the last "
               "one passed to the encoder is ignored (rtp::VideoSendController, `min_keyframe_request_interval`; "
               "issue #151; the library default since #580). The outage monitor's own request is not filtered.",
         verification="Pass threshold derived analytically: each unrecoverable loss costs libwebrtc's decode time-out "
                      "(3 s from the last released frame) + RTT + keyframe transit, about 93 frames, so the longest freeze is "
                      "3.1 s and 2% frame loss leaves 28-49% of frames decodable on seeds 1-5 (3-10% without keyframe "
                      "requests). The v1 repair window gave 83% and 0.5 s until #167 deleted it. "
                      "[#335](../../fixes/335-keyframe-packets.md): a 1 fps keyframe that took 0.5 s to arrive was asked "
                      "for again at 400 ms because only its first packet counted as a keyframe packet; with every packet "
                      "counted, the 1 fps repro matches libwebrtc (no keyframe requests, mouth-to-ear p95 about 860 ms). "
                      "#818: the discontinuity trigger, the keyframe method and the FIR sequence number are pinned by "
                      "RtpReceiver.ASequenceNumberDiscontinuityRequestsKeyframesUntilANewerKeyframeReanchors, "
                      "KeyframeRequest.EveryRequestUsesTheConfiguredMethod, RtcpEngine.FirSequenceNumbersStartAtOne and "
                      "LibwebrtcDefaults.TheSequenceDiscontinuityRuleRunsForCodecsOtherThanH264 / KeyframeRequestsArePli; "
                      "the bench's H.264 stream sends PLI only, as libwebrtc's, so no metric moves."),
    dict(id="3.7", slug="protection-selection", title="NACK/FEC protection selection",
         problems=["Picking a protection level -> loss-rate-driven trade-off between FEC and NACK, based on RTT"],
         headers=["loss/fec_controller.h"], unit=["FecController"], e2e=["E2E_Protection"],
         bench=["loss/FecControllerDefault_UpdateFecRates", "loss/Q_Protection_adaptive_rtt300"],
         scenario="The NACK/RTX/FEC video simulation with the controller choosing FEC every 500 ms from measured "
                  "loss, at RTT 18, 20, 80 and 300 ms with 5% loss; in the session, `issue_29_ulpfec_over_red`.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#3-packet-loss) for the differences from libwebrtc. "
               "A port of libwebrtc M157's `FecControllerDefault` (`modules/video_coding/fec_controller_default.cc`) "
               "with `media_opt_util.cc`'s `VCMLossProtectionLogic`, `VCMFecMethod` and `VCMNackFecMethod` and the "
               "`kFecRateTable` of `fec_rate_table.h`, copied value for value (#29). The method follows what the "
               "stream has on (`kNackFec`, `kFec`, `kNack`, `kNone`); the loss is the max of the last 10 one-second "
               "windows; the delta rate comes from the table by effective bits per frame and loss, at least 20% "
               "with loss and more than one packet per frame, at most 128/255; key frames get at least twice that "
               "and the loss; `kNackFec` sends no delta FEC below 20 ms RTT, none at all for small frames under "
               "200 ms RTT (`BitRateTooLowForFec`), and spreads FEC over up to 6 frames by RTT. The rates are "
               "converted to a share of the media packets (`ConvertFECRate`) and the masks are random. "
               "`rtp::Session` owns one per video send stream, as `RtpVideoSender` does: it calls `UpdateFecRates` "
               "when the target, the fraction lost or the RTT changes (`CongestionControlHandler::GetUpdate`), with "
               "the video payload rate, the sent frame rate and GoogCC's fraction lost, and sets the "
               "delta and key parameters on the sender; `UpdateWithEncodedData` runs on every frame and "
               "`SetVideoEncodingData` on each encoder reconfiguration. The encoder rate half of `UpdateFecRates` "
               "(the measured protection share, capped at 0.5) is `SendSideController::AllocatedVideoTarget`. "
               "The sent frame rate is `SendStatisticsProxy::GetSendFrameRate` (`video/send_statistics_proxy.cc`, "
               "#710): libwebrtc's `RateTracker` (`sm::RateTracker`, 10 buckets of 100 ms) counts each encoded frame, "
               "starts at the first captured frame (`OnIncomingFrame`), reports 0 for its first 100 ms and is rounded; "
               "`Session` exports it as `video_encode_frame_rate`. "
               "Remaining difference: `_corrFecCost` is not computed, since M157 reads it nowhere.",
         verification="Unit tests (`FecController`) check the table rates against values worked out by hand from "
                      "`kFecRateTable`, the first-partition minimum, the 128/255 cap, the 10 s max filter, the "
                      "20 ms NACK-only threshold, `BitRateTooLowForFec`, `max_fec_frames` and the packets-per-frame "
                      "filter. `E2E_Protection.AdaptiveAcrossRtt`: NACK only at 18 ms RTT (overhead 5%), FEC from "
                      "20 ms (overhead 45% at 20 ms, 39% at 80, 35% at 300; libwebrtc's table at 1.9 Mbps and 5% loss), "
                      "on time 99.3% or more. On `issue_29_ulpfec_over_red`, seeds 1-10, FEC goes from 375 to 392 "
                      "kbps (libwebrtc 444) and video e2e p95 from 174 to 242 ms (libwebrtc 232). "
                      "The sent frame rate (#710): `RateTracker` ports libwebrtc's `rate_tracker_unittest.cc`, and "
                      "`Session.SentFrameRateIsSendStatisticsProxysRateTracker` and "
                      "`VideoSendController.SentFrameRateStartsAtTheFirstCapturedFrame` check the rate the protection "
                      "logic takes; on `issue_29_ulpfec_over_red` seeds 1-80 the means stay within seed spread "
                      "(`video.avg_kbps` 973.6 -> 976.9, libwebrtc 986.8; FEC 498 -> 494 kbps). "
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
               "The buffer is sized as libwebrtc's PacketBuffer (issue #76 group 6; the fixed-window v1 was deleted in #167): 512 slots "
               "doubling to 2048 when another unassembled packet maps to a slot, a clear plus a keyframe request when it is "
               "full at 2048, and a restart when a packet is 1024 behind the lowest sequence number. Packets leave at "
               "frame assembly, and the receiver also calls ClearTo(last sequence number) when a frame leaves the scheduler (issue #101): "
               "it drops the packets up to it and ignores older ones; padding is kept only within a quarter of the buffer behind an insert. "
               "`missing_packets` (on in a session's H.264 video streams, as libwebrtc, issue #453; on by default in a bare PacketBufferConfig too since #580, which a caller turns off for another codec): libwebrtc's H.264 rule in FindFrames and UpdateMissingPackets "
               "(packet_buffer.cc): the sequence numbers not yet received below the newest one (at most 1000 behind it) are kept, "
               "a complete frame without an IDR slice is held while one of them is at or before its first packet, a released "
               "frame forgets the holes up to its last packet, padding fills a hole, ClearTo forgets the holes below it, and a "
               "packet that arrives later releases the held frames it makes contiguous (FindFrames' walk forward). "
               "Frame start (issue #847): a frame opens at the first of the contiguous packets that share its timestamp, "
               "if that one is a start, even when an earlier packet of the frame is missing, as libwebrtc's H.264 walk back in "
               "FindFrames (an IDR slice whose parameter-set packet is lost is handed out alone, as a keyframe). libwebrtc M157 "
               "uses `h26x_packet_buffer.cc` for H.264 only under a field trial that is off by default.",
         verification="No defects found. Issue #453: with `sps_pps_tracker on` and `h264_missing_packets on`, `high_jitter` "
                      "seeds 1-10 keyframe requests 1.8 -> 1.2, the same as libwebrtc on 10 of 10 seeds. Made the session default "
                      "for H.264 by the #453 flip (see docs/design/libwebrtc-parity.md). Issue #847: the H.264 assembly "
                      "rules compared with packet_buffer.cc and h26x_packet_buffer.cc; the wait for a missing packet before "
                      "an IDR start (`may_continue_frame`) removed as libwebrtc has none (unit tests "
                      "`PacketBuffer.IdrStartOpensTheFrameWhenItsParameterSetPacketIsMissing` and "
                      "`RtpReceiver.AKeyframeWhoseParameterSetPacketIsLostIsDeliveredFromItsIdr`); suite unchanged."),
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
               "(CompactNtpRttToTimeDelta, group 4). When the payload frequency changes the jitter is rescaled (ReviseFrequencyAndJitter, issue #101). "
               "With SessionConfig::per_stream_rtt (on by default, as libwebrtc, since #484 landed) the session aggregates the RTT as libwebrtc's per-stream RTCPReceiver::RttStats and "
               "ModuleRtpRtcpImpl2::PeriodicUpdate (issue #568): each send stream keeps the samples of its media "
               "SSRC's block only, gives its last one to CallStats and its packet history every second, takes its "
               "NACK RTT from that or its average, and GoogCC gets each stream's packet RTT, video first. "
               "With SessionConfig::receive_statistics_v2 (on by default, as libwebrtc, since #575's flip) "
               "the sequence and report rules are StreamStatisticianImpl's (issue #575): a packet more than the max "
               "reordering threshold (450, kMaxPacketAgeToNack, for video with NACK; kDefaultMaxReorderingThreshold 50 "
               "for audio, RTX and video without NACK) from the highest sequence number is held, and if the next packet "
               "follows it the stream restarted and the gap is no loss (UpdateOutOfOrder); fraction lost is "
               "255 * lost / expected, and a negative cumulative loss reports 0 (MaybeAppendReportBlockAndReset).",
         verification="No defects found; RTT min 60.6 ms against 60 ms configured. Issue #575: libwebrtc's "
                      "StreamRestartDoesntCountAsLoss and reordering cases pass as RtcpStats unit tests with v2 on."),
    dict(id="4.3", slug="delay-manager", title="Audio target delay estimation (DelayManager)",
         problems=["Variable packet arrival (audio) -> NetEq adaptive jitter buffer"],
         headers=["timing/delay_manager.h", "timing/neteq_delay.h"],
         unit=["DelayManager", "NetEqHistogram", "UnderrunOptimizer", "ReorderOptimizer", "PacketArrivalHistory",
               "NetEqDelayManager", "AudioJitterBufferNetEqV2"],
         e2e=["E2E_DelayManager"],
         bench=["timing/DelayManager_Update"],
         scenario="The audio jitter buffer simulation with jitter stepping from 5 ms to 40 ms half way; the target "
                  "delay must follow.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "A histogram with forgetting factor 0.983 per packet: rising jitter is learned within a few "
               "packets, while falling jitter takes several seconds, so the buffer does not oscillate. "
               "That `DelayManager` was the v1 jitter buffer's; since #817 removed version 1 it has no caller in the library. "
               "NetEq v2 (issue #62, the test bench's `neteq v2`; the session's `AudioStreamConfig` and the test bench default to it since #62, `AudioJitterBufferConfig` on its own since #579, and it is the only jitter buffer since #817 removed version 1): "
               "`NetEqDelayManager` ports libwebrtc M157's modules/audio_coding/neteq/ histogram.cc (Q30 buckets, "
               "Q15 forget factor from 0 with start_forget_weight 2), underrun_optimizer.cc (95th percentile of the "
               "largest relative delay of each 500 ms interval, 100 buckets of 20 ms, forget factor 0.983), "
               "reorder_optimizer.cc (forget factor 0.9993, 20 ms per percent of reordered packets lost), "
               "packet_arrival_history.cc (delay against the fastest in-order packet of the last 2 s of RTP time), "
               "delay_manager.cc (max of the two, 80 ms until the first estimate), delay_constraints.cc (minimum "
               "delay, at most 75% of a 200-packet buffer) and DecisionLogic::PacketArrived. Arrival times are on "
               "NetEq's 10 ms TickTimer, which advances once per GetAudio, and the packets before the first GetAudio "
               "are dropped from the statistics as NetEqImpl's SoftReset on the first codec does. The operations "
               "that use this target are in 4.5. NetEq::Config's max_packets_in_buffer "
               "(200) and min_delay_ms (0) are from api/neteq/neteq.h, which is not in compare/webrtc/reference/ "
               "(#57).",
         verification="No defects found. NetEq v2: libwebrtc's histogram, underrun, reorder, packet arrival history "
                      "and delay manager unit tests pass. On `high_jitter` (seeds 1-10) audio concealment falls from "
                      "1.17-1.64 % to 0-0.1 % (libwebrtc 0.07-0.27 %) and the jitter buffer delay p95 rises from "
                      "72-88 ms to 104 ms (libwebrtc 100-130 ms); `issue_62_neteq_v2_high_jitter` locks it in."),
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
               "audible. This was the v1 jitter buffer's DSP; the jitter buffer uses NetEq's (NetEqTimeStretch, see 4.5), "
               "and since #817 removed version 1 `TimeStretcher` has no caller in the library.",
         verification="Defect found in integration: the jitter buffer only decoded audio until it had 10 ms "
                      "ready, but WSOLA needs 30 ms, so stretching never ran and latency stayed at 600 ms after "
                      "a stall. Fixed by decoding ahead only when a stretch is wanted (AudioJitterBuffer::"
                      "MaybeStretch); recovery now takes ~0.5 s."),
    dict(id="4.5", slug="audio-jitter-buffer", title="Adaptive audio jitter buffer (NetEq-lite)",
         problems=["Variable packet arrival (audio) -> NetEq adaptive jitter buffer with time-stretching"],
         headers=["timing/audio_jitter_buffer.h", "timing/neteq_expand.h"],
         unit=["AudioJitterBuffer", "AudioJitterBufferNetEqV2", "NetEqBufferLevelFilter", "NetEqDecisionLogic",
               "NetEqRandomVector", "NetEqExpand", "NetEqMerge", "NetEqTimeStretch"],
         e2e=["E2E_AudioJitterBuffer"],
         bench=["timing/AudioJitterBuffer_10msPull_16k", "timing/Q_AudioJitterBuffer_jitter25_loss2"],
         scenario="60 s of speech, 20 ms packets, 25 ms Gaussian jitter (with reordering), 2% loss. The adaptive "
                  "buffer is compared with a fixed 20 ms and a fixed 200 ms buffer.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Decoded PCM can be inserted (decode-on-insert, for decoders without a duration); with "
               "a decoder that gives the frame's duration (rtp::AudioDecoder::PacketDuration, "
               "AudioDecoder::PacketDuration in libwebrtc) the frame is inserted encoded and decoded when it is "
               "played, in timestamp order, as NetEqImpl::DecodeLoop decodes (#783): a stateful decoder (Opus) "
               "then sees NetEq's sequence, and copies replaced by a higher priority or discarded as late are "
               "never decoded. On issue_533 seed 1 that makes the first expand's input, and so its voiced/unvoiced "
               "split and mute slope, the same as libwebrtc's. Statistics use WebRTC getStats names. "
               "NetEq v2 (issue #62, `neteq v2`; the session and test bench default since #62) chooses the operation as libwebrtc M157's "
               "modules/audio_coding/neteq/decision_logic.cc and NetEqImpl::GetDecision/GetAudioInternal "
               "(neteq_impl.cc) do, in `NetEqDecisionLogic` (timing/neteq_delay.h): playout starts with the first "
               "packet (new codec: a normal decode, no wait for the target); with the expected packet available "
               "and the last mode not an expand, fast accelerate at 4x and accelerate at the arrival history's "
               "playout delay >= target + its largest arrival delay + 20 ms, preemptive expand below the target, "
               "the last two at most once per kMinTimescaleInterval (5 ticks after a successful stretch, 6 after a "
               "reset); decoding is postponed while the buffer (span plus the newest packet's waiting time) is "
               "under 50 % of the target and the expand is muted below 0.5; a future packet is merged once the "
               "expand has covered the gap or the buffer is above target + 50 ms, otherwise the expand goes on; "
               "a missing packet with a later one buffered starts an expand after normal playout. The "
               "extraction rules are NetEq's: nothing is decoded while 10 ms is ready, except for merge and the "
               "stretches; accelerate needs 30 ms decoded (else normal, decoding 20 ms first); preemptive expand "
               "decodes up to 20 ms and borrows the rest of its 30 ms from the samples just played, inserting the "
               "period after them. BufferLevelFilter (buffer_level_filter.cc, Q8, factor 251-254 by target) is "
               "ported and exported as `filtered_buffer_level_samples`; M157 uses it for no decision. "
               "The packet buffer holds NetEq::Config::max_packets_in_buffer (200) packets and is flushed "
               "completely when a packet arrives to a full buffer, as PacketBuffer::InsertPacket without the "
               "smart-flushing field trial (issue #331). The flush takes "
               "NetEqImpl's new_codec_ path (issue #773, docs/fixes/773-neteq-flush-reset.md): the packets up to the "
               "next decision skip the controller's statistics and DiscardOldPackets, the sync buffer is kept, and "
               "that decision (after the early return for a full output block) restarts the timestamp at the next "
               "packet and soft-resets the controller (target back to 80 ms). "
               "Muted state (issue #574) is NetEqImpl::GetAudioInternal's (neteq_impl.cc:682-698): once the expand "
               "has faded to silence and no packet is waiting, a muted frame is played without the decision logic or "
               "the expand, the playout timestamp moves on by the frame and the samples count as concealed noise "
               "(`muted_frames`); `enable_muted_state` is off in AudioJitterBufferConfig, as NetEq::Config, and on "
               "in the session's audio receive, as ChannelReceive (channel_receive.cc:107). "
               "The stretch DSP is NetEq's (NetEqTimeStretch below), fed as NetEqImpl::DoAccelerate and "
               "DoPreemptiveExpand do: the decoded audio, or 30 ms borrowed from the end of the sync buffer. "
               "Differences that remain: fast accelerate is a plain accelerate, as "
               "NetEq::Config::enable_fast_accelerate defaults to false (api/neteq/neteq.h, missing from "
               "compare/webrtc/reference/, #57). "
               "The v2 expand is libwebrtc M157's Expand (expand.cc, random_vector.cc and the DspHelper pieces it "
               "uses), as `NetEqExpand` (timing/neteq_expand.h): AnalyzeSignal's three lags from the 4 kHz "
               "correlation peaks and the distortion, the two expansion vectors, the order-6 AR model of the "
               "unvoiced part, the voice mix factor and the mute slope (onset above an amplitude ratio of 1.5); "
               "Process appends one lag per call, cycling through the lags, with the slope floors of the 4th and "
               "8th calls and the stuck-gain guard, and its mute factor is what DecisionLogic's PostponeDecode "
               "reads. NetEqImpl::DoExpand's loop (whole periods until the sync buffer holds the output plus the "
               "5/8 ms overlap), the overlap kept back as lookahead (FlushBuffers, GetAudioInternal) and Normal's "
               "first decode after an expand (one more expansion, 1 ms crossfade, unmute by at least 0.64 per "
               "20 ms) are ported, and Expand is reset after a merge, accelerate, preemptive expand or that "
               "decode. All of Expand is libwebrtc's fixed-point arithmetic on int16 samples, bit-exact with "
               "expand.cc (#783): the 4 kHz correlation normalized to 14 bits with the fixed-point peak search, "
               "the distortion search, WebRtcSpl_LevinsonDurbin for the Q12 AR filter, the residual gain, the "
               "unvoiced part through WebRtcSpl_FilterARFastQ12 and the Q14 crossfade and muting. BackgroundNoise is ported as NetEqBackgroundNoise "
               "(background_noise.cc): after each GetAudio that left decoded audio unchanged (normal, failed "
               "accelerate or preemptive expand) it reads the last 256 samples of the sync buffer, and when "
               "their energy is under the update threshold (500000 at start, then the lowest seen, rising 4x in "
               "4 s while the signal is louder, and kept within 60 dB of the decaying maximum) and the order-8 "
               "LPC residual is flat (5 * residual >= 16 * energy), it saves the filter, energy and gain, in "
               "libwebrtc's integer arithmetic (the Levinson-Durbin recursion in double, rounded to Q12). As in "
               "M157, the noise Expand mixes in is scaled by a mute factor that each expand period sets to 0 and "
               "nothing raises, so it adds nothing; the estimate's energy (2500 until the first save) sets where "
               "Normal's first decode after an expand starts unmuting: sqrt(noise energy / energy of its first "
               "8 ms), or Expand's mute factor if higher. Merge is ported as NetEqMerge (merge.cc, one channel): "
               "the packet after a lost one is joined by one more unmuted expansion, repeated after the sync "
               "buffer's unplayed samples to 202 ms * fs/8000; the decoded audio goes in at the lag where its "
               "first 10 ms best correlate with that signal at 4 kHz (up to max_lag, and far enough on that the "
               "sync buffer then holds 10 ms plus the overlap and all its old samples), overwriting the unplayed "
               "expansion. It starts from the larger of Expand's mute factor and SignalScaling's sqrt(expanded / "
               "decoded energy) over 8 ms (integer arithmetic), unmutes by at least 4194 / (fs/8000) in Q20 per "
               "sample or to full gain within the frame, and is crossfaded in linearly over up to 60 * fs/8000 "
               "samples. The samples a merge adds or removes are a concealed-samples correction "
               "(StatisticsCalculator, a negative one held against later additions). After the expansion it is "
               "libwebrtc's fixed-point arithmetic on int16 samples, bit-exact with merge.cc (#783): the Q12 "
               "downsampling, the 4 kHz correlation normalized to 14 bits with the fixed-point peak search, and "
               "the Q14 ramp, unmute and crossfade. Accelerate and PreemptiveExpand are ported as NetEqTimeStretch "
               "(time_stretch.cc, accelerate.cc, preemptive_expand.cc, one channel): the input is downsampled to "
               "4 kHz with the filter delay compensated, the strongest autocorrelation peak between lags 10 and "
               "59 gives the pitch period, and the periods either side of 15 ms are compared. A VAD calls the "
               "block passive when their energy per sample is at most 8x the background noise energy (75000 "
               "before the first estimate), and then any period is used (kSuccessLowEnergy, NetEq's "
               "kAccelerateLowEnergy / kPreemptiveExpandLowEnergy modes); otherwise the correlation must exceed "
               "0.9 in Q14 (0.5 for fast accelerate), and preemptive expand also needs at most 15 ms already "
               "played. Accelerate crossfades the period before 15 ms into the one after it; preemptive expand "
               "repeats the period before max(15 ms, played part); the crossfade is AudioVector::CrossFade's "
               "Q14 ramp. All of it is libwebrtc's fixed-point arithmetic on the int16 samples (#644): the "
               "WebRtcSpl downsampling, autocorrelation, energies, VAD and correlation, DspHelper::PeakDetection "
               "with the table ParabolicFit, and the crossfade rounded as AudioVector's ((a x + (16384 - a) y + "
               "8192) >> 14); fast accelerate reports one period as its length change, as TimeStretch::Process. "
               "It is bit-exact with libwebrtc's time_stretch.cc, accelerate.cc and preemptive_expand.cc compiled "
               "from compare/webrtc/reference/ (`NetEqTimeStretch, BitExactWithLibwebrtc`: 48 rate x mode x "
               "background-noise cases, each over 168 signals). Where NetEq's GetAudioInternal fails on a stretch error "
               "(input under 30 ms, or no new samples past the overlap), the input is kept unchanged. "
               "Opus in-band FEC (issue #62, version 2): the receiver asks the app's `AudioDecoder` whether a "
               "frame carries LBRR (`FecDuration`, AudioDecoderOpusImpl::ParsePayload in audio_decoder_opus.cc) "
               "and inserts the decoded copy at the previous frame's timestamp with codec level 1; the buffer "
               "keeps one packet per timestamp, the higher Packet::Priority (codec level, then RED level), as "
               "PacketBuffer::InsertPacket, and a late FEC frame is not a late discard. A late packet goes into the "
               "buffer as in PacketBuffer::InsertPacket; GetDecision drops it with DiscardOldPackets' 5 s horizon "
               "(IsObsoleteTimestamp), and a packet more than 5 s older than playout stays and is played as a "
               "future packet: DecisionLogic's uint32 timestamp leap makes it too early, so NetEq expands and "
               "then merges back to it; after each extraction all older packets go (DiscardAllOldPackets) (#783). "
               "Which frames carry LBRR "
               "is libopus's own decision in the test bench (libopus 55513e81, #457; `testbench/opus_encoder.h`), "
               "and the bench's decoder finds it with WebRtcOpus_PacketHasFec's parse: the encoder's loss input is ported "
               "(ChannelSend::OnReportBlockDataUpdated, PacketLossFractionSmoother, SetProjectedPacketLossRate). "
               "Verified by `issue_62_opus_fec_low_rate` (no LBRR at 12 kbps, 10% loss) and the "
               "OpusEncoder unit tests. "
               "Comfort noise (comfort_noise.cc, NetEq's kRfc3389Cng and kCodecInternalCng) is not ported: it is "
               "unreachable on libwebrtc's defaults (issue #827). RFC 3389 CN needs a CN payload type, which Opus "
               "never gets (allow_comfort_noise false, audio_encoder_opus.cc:240; CN at 8 kHz only, "
               "webrtc_voice_engine.cc:1538-1553), and codec-internal CNG needs a 1-2 byte Opus payload "
               "(opus_interface.cc DetermineAudioType), which only DTX sends, off unless the SDP has usedtx=1 "
               "(audio_encoder_opus.cc:257). The session registers no CN payload type and the test bench's "
               "encoder never sends a DTX-sized payload (`OpusEncoder, DefaultsSendNoDtxPayloads`); an app that "
               "turns DTX on gets those frames decoded as normal frames. Recorded in the parity inventory (4.4).",
         verification="Issue #256 (fix record docs/fixes/256-neteq-unsupported-rate.md): at a rate other than "
                      "8, 16, 32 or 48 kHz NetEq v2's expand and time stretch indexed outside their buffers "
                      "(ASan); version 2 now discards those packets and plays silence, as libwebrtc, where no "
                      "decoder exists at such a rate. "
                      "Issue #298 (fix record docs/fixes/298-audio-24khz-plays.md): the test bench used the capture "
                      "rate as the codec clock, so a 24 kHz call lost all its audio; it now encodes 12, 24 and "
                      "44.1 kHz captures at Opus's 48 kHz clock as libwebrtc does, and `issue_256_audio_24khz` "
                      "measures jitter-buffer p95 112 ms, concealment 0.25 %, mouth-to-ear p95 194 ms (seeds 1-5; "
                      "libwebrtc 129, 0.39, 219, the same gaps as the 16 kHz calls). "
                      "See 4.4 for the decode-ahead defect. A first comparison test was wrong: a fixed 20 ms "
                      "buffer without acceleration does not conceal late packets, it waits for them, so its "
                      "latency ratchets up (to 4x its target). The test now checks concealment against the loss "
                      "rate and delay against both fixed buffers. "
                      "NetEq v2 operations (issue #62): libwebrtc's decision_logic and buffer_level_filter unit "
                      "tests pass (CNG cases excluded, no CNG here). With `neteq v2` on the whole suite (seeds "
                      "1-3), the audio gaps against libwebrtc's 10-seed means go from 112 to 94 (38 closed, 20 "
                      "opened). `audio_only_lossy` jitter buffer delay p95 60 -> 72 ms and mouth-to-ear 133 -> "
                      "145 ms (libwebrtc 81 and 147); `issue_62_neteq_v2_audio_only_lossy` locks it in. "
                      "NetEq v2 Expand (issue #62): expand_unittest.cc's four tests pass (DelayedPacketOutage, "
                      "LostPacketOutage, CheckOutageStatsAfterReset, Muted; with a synthetic voiced signal for "
                      "testfile32kHz.pcm), plus tests of the lags, the slope floors, the onset rule and the "
                      "jitter buffer's expand loop. With `neteq v2` on the 19 base scenarios against libwebrtc's "
                      "values on the same seeds (1-5), the audio gaps go from 46 to 42. `cross_traffic` longest "
                      "audio interruption goes from 10-20 ms to 10-90 ms (libwebrtc 30-100 ms on those seeds); "
                      "`issue_62_neteq_v2_cross_traffic` locks it in. "
                      "NetEq v2 Merge (issue #62): merge_unittest.cc's Process test passes (input lengths 1, 2, "
                      "3, 80, 81 and 160 at 8 kHz, synthetic voiced signal), plus tests of the alignment, "
                      "SignalScaling, the unmute rates, the cut of a long unplayed part and the jitter buffer's "
                      "merge. With `neteq v2`, seeds 1-10: `audio_only_lossy` jitter buffer delay p95 75.0 -> 68.3 ms "
                      "and mouth-to-ear p95 147.6 -> 140.9 ms (libwebrtc 81 and 146.8), time-stretch 0.24 -> 0.51 % "
                      "(libwebrtc 0.39): the merge now overwrites the unplayed expansion instead of playing it "
                      "before the packet. `high_jitter` and `cross_traffic` are unchanged but for `high_jitter` "
                      "jitter buffer delay p50 80.4 -> 82.0 ms (libwebrtc 85). "
                      "NetEq v2 Accelerate/PreemptiveExpand (issue #62): time_stretch_unittest.cc's "
                      "CreateAndDestroy passes; its Accelerate tests read testfile32kHz.pcm, so synthetic voiced "
                      "signals and noise test the period, the 0.9 and 0.5 thresholds, the VAD against the noise "
                      "estimate, preemptive expand's old-data rule, the crossfade and the jitter buffer's whole "
                      "periods. With `neteq v2`, seeds "
                      "1-10: `audio_only_lossy` time-stretch 0.51 -> 0.44 % (libwebrtc 0.39), `cross_traffic` "
                      "2.29 -> 2.06 % (libwebrtc 2.37); the other audio means move by under 2 %. "
                      "NetEq v2 as the default (issue #62): on the whole suite (seeds 1-10, libwebrtc from main's "
                      "parity-data) 361 audio means move toward libwebrtc and 94 away, and the parity report goes "
                      "from 319 to 237 gaps (109 closed, 27 opened). `audio_only_lossy` concealed 1.62 -> 0.51 % "
                      "(libwebrtc 0.46), jitter buffer delay p95 44 -> 71 ms (81); `cross_traffic` jitter buffer "
                      "delay p95 182 -> 373 ms (382). What stays apart is mostly mouth-to-ear outside the jitter "
                      "buffer (`bufferbloat` 199 vs 176 ms with the jitter buffer delay matched); "
                      "`issue_62_neteq_default` locks the default in."),
    dict(id="4.6", slug="video-jitter", title="Video jitter estimation and render timing",
         problems=["Variable packet arrival (video) -> video jitter buffer, frame buffer and render-time estimation"],
         headers=["timing/video_jitter_estimator.h", "timing/vcm_timing.h"], unit=["VideoJitter", "VcmTiming"],
         e2e=["E2E_VideoJitter"],
         bench=["timing/VideoJitterEstimator_Update", "timing/Q_VideoJitter_late_frames"],
         scenario="60 s of 30 fps video with 3-6 KB frames and a 25 KB keyframe every 2 s over a 2.5 Mbps "
                  "bottleneck with 10 ms jitter. Each frame's render time comes from VideoTiming fed by the "
                  "estimator; a frame is late if it arrives (plus decode time) after its render time.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#4-jitter-and-timing) for the differences from libwebrtc. "
               "Timing v2 (the default) is libwebrtc's `video/timing/` and `VCMTiming` with their defaults; "
               "rows 4.1 and 4.2 there list each part, re-checked line by line on #645. Timing v1 (`timing v1`, "
               "not the default) differs from libwebrtc: its jitter term is 3 sigma with no offset, where "
               "libwebrtc uses 2.33 sigma - 30 ms plus 10 ms of OS jitter.",
         verification="Defect found: with libwebrtc's -30 ms offset the jitter delay collapsed to 1 ms for 15 ms "
                      "network jitter, so about 600 of 1 800 frames arrived after their render time. With the new "
                      "default, late frames fell from 2.8% to 0.1% in the timing e2e test, at 134 ms mean delay. "
                      "Provisional lock (Owner, PR #180): `issue_76_tcc_v2_high_jitter` holds video e2e delay p95 "
                      "at < 1300 ms (libwebrtc 136 ms) until the receive-side delay under jitter with reordering is "
                      "fixed in #24. "
                      "Timing v2 ([fix record](../../fixes/24-timing-v2.md), partial for #24; `VideoTimingConfig`'s default since #579): `timing.version = 2` "
                      "ports libwebrtc M157's JitterEstimator, TimestampExtrapolator, RttFilter, "
                      "DecodeTimePercentileFilter and VCMTiming with their defaults (2.33 sigma - 30 ms, 10 ms OS "
                      "jitter, +0.9 x RTT after 3 NACKed frames); libwebrtc's JitterEstimator SteadyStateConvergence "
                      "test gives its 54 ms here. With every v2 switch on, `high_jitter` video e2e delay p95 is "
                      "130-199 ms on seeds 1-10 (timing v1 162-313, libwebrtc 122-152) and p50 95-157 ms "
                      "(timing v1 158-245, libwebrtc 92); `issue_24_timing_v2_high_jitter` locks it in. On `clean_link` with every v2 "
                      "switch on, video e2e delay p50 is 71-79 ms on seeds 1-40 (timing v1 59-63, libwebrtc 74.8) and time to "
                      "first frame 52 ms (timing v1 68, libwebrtc 53.7); `issue_24_timing_v2_clean_link` locks it in. "
                      "Re-checked against `compare/webrtc/reference/` on #645: libwebrtc's unit arithmetic in three places "
                      "(the 90 kHz RTP delta truncates to the microsecond, the frame rate is whole millihertz, the "
                      "RTT filter's short average divides), pinned by `unit.VcmTiming`'s "
                      "`IfdvRtpDeltaTruncatesToTheMicrosecond` and `JitterFrameRateIsWholeMillihertz`; the suite "
                      "moves by at most 0.2 ms. "
                      "#674 checked the last two files: the frame-rate mean is `RollingAccumulator::ComputeMean`, a "
                      "Welford running mean (ported as `RollingMean`, pinned by "
                      "`JitterFrameRateMeanIsWelfordsAsRollingAccumulator`; sum / count differed in the last bit), and "
                      "the 10 s reset is `frame_helpers.cc`'s `FrameHasBadRenderTiming`, the same rule as ours. "
                      "Default v2 since #24 ([fix record](../../fixes/24-timing-v2-default.md)): `rtp::VideoStreamConfig` "
                      "and the test bench run timing v2 (`timing v1` still selects the library's own). On top of the #61 "
                      "flip it closes 88 parity gaps and opens 11 (seeds 1-10); `issue_76_tcc_v2_high_jitter` is back "
                      "at `< 300` (130-199 ms, libwebrtc 130-218) and `issue_24_timing_v2_default` locks the default in. "
                      "Explained difference (Owner, PR #216), tracked to #224: on congestive links "
                      "(`issue_61_googcc_glue_congestive`, `issue_12_lossbwe_congestive`) video e2e delay p95 is 808 ms "
                      "against libwebrtc's 420. Timing v2 renders the frames timing v1 dropped as late (1194 rendered, "
                      "libwebrtc 1195), and those waited longest in the sender's pacer (pacer p95 of rendered frames 609 ms); "
                      "e2e p50 matches (204 vs 196). "
                      "Tracked to #230: `issue_127_pacer_v2_jitter_reorder_loss` has 3.3 freezes (753 ms) against libwebrtc's "
                      "1.1 (240 ms). Both receivers' jitter delay falls to 0 from 17 s (yardstick #228): reordered originals "
                      "NACKed at once flag 90-100% of frames as retransmitted, so JitterEstimator's frame rate drops below "
                      "5 fps and GetEstimate returns 0, in libwebrtc too. The extra freezes are repairs slower than the "
                      "freeze threshold on our side, with a 13% higher send rate. "
                      "Owner rule on PR #216 (just above our seed-1 value, never above libwebrtc's worst seed in 1-10): "
                      "`issue_12_lossbwe_policer` `net.loss_pct < 14` (ours 13.18, libwebrtc worst 15.97) and "
                      "`video.freeze_total_ms < 6000` (4685, 15854); `issue_124_burst_loss_v2` `video.e2e_delay_p95_ms < 314.08` "
                      "(304.4; capped at libwebrtc's worst seed)."),
    # ------------------------------------------------------------------ 5
    dict(id="5.1", slug="transport-feedback", title="Transport-wide CC feedback (and REMB)",
         problems=["Fine-grained per-packet feedback -> transport-cc (Transport-Wide CC) RTCP feedback",
                   "Receiver-side bandwidth hints -> REMB (legacy)"],
         headers=["cc/transport_feedback.h"], unit=["TransportFeedback"], e2e=["E2E_TransportFeedback"],
         bench=["cc/TransportFeedback_Build100_SerializeParse"],
         scenario="7 500 packets over 5% loss with jitter (reordering); feedback every 50 ms serialized to the "
                  "real wire format, parsed and joined with the send history. Each packet must be reported received "
                  "once (a reordered packet first reported lost comes back received, as libwebrtc's generator "
                  "re-reports it), with arrival-time error within the 250 us quantization of the wire, and each time since the feedback's base "
                  "time handed to the estimators rounded down to 1 ms, on the adapter's local time base, as libwebrtc's "
                  "TransportFeedbackAdapter (#365, #569).",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Status chunks use run-length coding for runs of 14 or more, otherwise 1-bit or 2-bit status "
               "vectors. Each arrival delta is rounded to the nearest 250 us tick, half away from zero, as "
               "rtcp::TransportFeedback::AddReceivedPacket does (#365; checked against libwebrtc's builder by sm_rtcp_interop). The builder is libwebrtc's TransportSequenceNumberFeedbackGenenerator: arrivals stay 500 ms, a "
               "reordered packet moves the window start back and is reported again, no gap older than 0x7FFE is "
               "reported, a message that cannot take another packet ends, and the interval follows 5% of the local "
               "send estimate (50-250 ms, 100 ms to start). Its v1 builder (each arrival once, fixed 50 ms) and the "
               "`tcc` switch were deleted in #167. "
               "The session sends the first periodic feedback after the generator's default 100 ms, "
               "and the interval from the local send estimate applies from then on: "
               "`Call::EnsureStarted` (call/call.cc) starts the task that runs "
               "`ReceiveSideCongestionController::MaybeProcess` before the Call hears its start rate, and "
               "`TransportSequenceNumberFeedbackGenenerator::Process` returns the delay to its next run (#217).",
         verification="No defects found; maximum arrival-time error 249 us. Issue #217: the first feedback went out "
                      "at 50 ms where libwebrtc's goes out at 100 ms, so the first probe estimate came from the first "
                      "cluster alone (`issue_120_rtx_payload_padding` seed 1: 913 kbps at 65 ms, libwebrtc 1608 at "
                      "115 ms); unit test `Session.TransportFeedbackFirstReportWaitsTheDefaultInterval`. Issue #569 part 2: the send "
                      "history ported as libwebrtc's TransportFeedbackAdapter (60 s window by creation time instead of "
                      "an 8192-packet cap, in-flight data above the last acknowledged sequence number, the local "
                      "receive-time base, no update for a feedback that finds no packet); unit tests "
                      "`TransportFeedbackAdapter.*`; the scenario suite is unchanged, metric for metric. Issue #897: "
                      "the send time in each result kept the pacer's microseconds, where libwebrtc's adapter takes "
                      "rtc::SentPacket::send_time_ms (transport_feedback_adapter.cc:184), so its estimators see whole "
                      "milliseconds; on `issue_150_gcc_v2_high_jitter_seed10` seed 10 the acked rate differed from "
                      "libwebrtc's at 1280 ms (470925 vs 471029 bps, yardstick #915) and matches it with the port; unit "
                      "test `TransportFeedbackAdapter.SendTimeIsWholeMilliseconds`."),
    dict(id="5.2", slug="delay-based-bwe", title="Delay-based bandwidth estimation (GCC)",
         problems=["Unknown or changing available bandwidth -> GCC: delay-based estimation"],
         headers=["cc/delay_based_bwe.h", "cc/aimd_rate_control.h"], unit=["DelayBwe", "DelayBweV2", "DelayBweLibwebrtc", "RobustThroughputEstimator", "AcknowledgedBitrateEstimator", "AimdRateControl", "BitrateEstimatorV2"], e2e=["E2E_DelayBwe"],
         bench=["cc/Gcc_OnFeedback_per_packet", "cc/Q_Gcc_capacity_steps"],
         scenario="Full closed loop (encoder, pacer, transport-cc, GCC) over a bottleneck whose capacity steps "
                  "2000 -> 800 -> 2500 kbps every 20 s, with a 200 KB drop-tail queue. Utilization and queueing "
                  "delay are measured over the second half of each phase.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "After a capacity increase GoogCC climbs back slowly, so the last phase has a lower utilization bar. "
               "The estimator is libwebrtc's; the library's own (`cc v1`: its InterArrival grouping, AIMD controller "
               "and acked-rate window) was deleted in #167, and with it the 200 ms queue bound of the 800 kbps phase "
               "(600 ms since, program.md 0.2: libwebrtc on `capacity_drop` has queue p95 279 ms and settles in 14.3 s). "
               "Issue #19 started the port of libwebrtc's GoogCC as a unit (#61): the "
               "detector's front end is `DelayBasedBwe::IncomingPacketFeedbackVector` with `InterArrivalDelta` "
               "(`BelongsToBurst`, reordered groups, feedback sorted by receive time, 2 s stream time-out). The "
               "trendline detector was already libwebrtc's. #15 adds the rest of the overuse path: the "
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
               "`AcknowledgedBitrateEstimatorInterface::Create`), and since #150 GoogCC feeds it with the receive-sorted "
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
         headers=["cc/loss_based_bwe_v2.h", "cc/send_side_bandwidth_estimation.h"],
         unit=["LossBweV2", "SendSideBweV2", "GccV2Loss"], e2e=["E2E_LossBwe"],
         bench=["cc/Q_LossBwe_15pct"],
         scenario="An 8 Mbps link with 15% versus 1% random loss: heavy loss must push the rate far below "
                  "capacity, light loss must not.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "The estimate is libwebrtc's code (#12; the library's own loss bands, `cc v1`, were deleted in #167): `LossBasedBweV2` (`loss_based_bwe_v2.h`, CreateConfig's "
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
                      "windows are at least 1 s and decisions are applied to the current target (that controller was "
                      "deleted in #167). Since #12 the unit tests port libwebrtc's `loss_based_bwe_v2_test.cc` cases with their numbers; the "
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
               "The controller is libwebrtc's GoogCcNetworkController (see the default below); the pacing rate is "
               "`GetPacingRates`' (5.6). rtp::SendSideController adds congestion-window pushback "
               "(cc/congestion_window_pushback.h, a port of libwebrtc's CongestionWindowPushbackController with "
               "its drop-frame-only mode): when the data in flight exceeds target x (min feedback RTT + 350 ms) "
               "the encoder drops every N-th captured frame (SendRates::frame_drop_interval, "
               "SendSideController::DropCapturedFrame()). The pacing rate is not reduced. "
               "As in libwebrtc (#411, D8), the drop is decided for a frame before the encoder sees its size "
               "(VideoStreamEncoder::OnFrame before MaybeEncodeVideoFrame), and an encoder reconfiguration's "
               "reallocation (OnEncoderConfigurationChanged -> BitrateAllocator::AddObserver, posted, so from the frame "
               "after the reconfiguring one) carries no reduce ratio, which ends the drops until the next network update. "
               "The reduce ratio is taken from the pushback target after it is clamped to GoogCC's minimum bitrate "
               "(goog_cc_network_control.cc:596-608), so once the pushback reaches that minimum the ratio stops "
               "changing, no further update re-applies it, and a reconfiguration's reset holds (#738). "
               "`SendSideConfig::control_handler` (scenario `control_handler on|off`, default on since #224/#204) adds libwebrtc's "
               "`CongestionControlHandler` (cc/control_handler.h, control_handler.cc) between GoogCC and the allocator: "
               "the allocation, and with it the encoder target and its protection share, changes only when the target, the "
               "reduce ratio, the loss fraction or the RTT changes, and the encoder pauses while the pacer queue is above 2 s. "
               "#166 ports goog_cc_network_control_unittest.cc against `GccController` "
               "(`GoogCcNetworkController`, 9 cases): the 8 `GoogCcNetworkControllerTest` cases the API can express pass "
               "(`RespectsConfiguredPacingFactor` through `SetPacingFactor`, #187), "
               "`OnNetworkRouteChanged` in its first half (a route change without a starting rate has no counterpart: "
               "`ResetEstimate` always takes a rate, and keeps the configured min and max), `ProbeOnRouteChange` with the "
               "route's maximum configured up front, and `TriggersTargetRateUpdateWhenBandwidthLimitedChanges` on the "
               "`AlrDetector` state (only libwebrtc's RFC 8888 feedback generator reads `is_bandwidth_limited`). "
               "`GoogCcScenario.NoCrashOnVeryLateFeedback` is ported at the controller level. Not ported: "
                              "`ProbeAfterRouteChangeWhenTransportWritable` (network availability belongs to the host stack's ICE), "
               "`LimitPacingFactorToUpperLinkCapacity` (a field trial, off in M157), and the other `GoogCcScenario` "
               "cases, which need libwebrtc's network emulation: the scenario suite and `sm_compare` cover their links "
               "(capacity steps, high loss, cross traffic, REMB caps), and the rest set field trials that are off by default "
               "(`WebRTC-CongestionWindow` sizes, `MaxRttLimit`, `SafeResetOnRouteChange`, `ResetOnAdapterIdChange`, "
               "`LossBasedControl`, `AddPacingToCongestionWindowPushback`) or send without transport-cc. "
               "**Default (#61):** a `rtp::Session` runs libwebrtc's components only: #167 deleted the v1 paths and "
               "the nine switches (`cc`, `pacer`, `tcc`, `rtcp`, `rtx`, `keyframe`, `packetization`, `allocator`, `audio`), "
               "so GoogCC, the PacingController, transport-cc feedback, RTCP report timing, the RTX history, the "
               "receiver's keyframe requests, NACK list and packet buffer, `RtpSenderVideo`'s payload sizing, "
               "`BitrateAllocator` and (in the test bench) libopus are libwebrtc's only. `GccConfig` and "
               "`PacerConfig` have no version any more. The outage monitor only follows the link: its pause/resume, "
               "keyframe and reset decisions were acted on under `cc v1` only (libwebrtc has none, #151). "
               "Guard rails changed with the `cc v1` deletion (program.md 0.2, #167, Owner sign-off pending): the "
               "closed-loop simulation's 800 kbps phase queue p95 (`E2E_DelayBwe`, `Q_Gcc_capacity_steps`) 200 -> 600 ms, "
               "its clean 2.5 Mbps audio delay p95 (`E2E_Pacer`, `Q_Pacer_audio_delay`) 60 -> 130 ms, and the "
               "resolution 20 s after a rise from 400 kbps (`E2E_VideoAdaptation`, `Q_Adaptation_bandwidth_drop`, both "
               "removed with the bits-per-pixel ladder in #819) 1280 -> 960; libwebrtc on the bench's `capacity_drop` has queue p95 279 ms and settles in 14.3 s, and "
               "on `clean_link` queue p95 74 ms. "
               "`timing` and `neteq` v2 were not in this flip: `timing` v2 joined with #24 (4.6), `neteq` v2 with #62. "
               "The test bench's Opus frame-size model, an approved deviation until #457, is replaced by libopus "
               "55513e81 (3.1). Explained differences: `outage_3s` `cc.queue_p95_ms` under all v2 (5.11, #151) "
               "and `issue_53_cwnd_pushback` render fps, from the receive-side `FrameScheduler` drop rule (#24; "
               "resolved when timing v2 became the default: 28.8-29.2 fps, libwebrtc 27.1-27.6). "
               "Provisional locks (PR #180): `issue_151_outage_3s_v2` (3.1, tracked to #151) and "
               "`issue_76_tcc_v2_high_jitter` (4.6; restored to `< 300` by #24). Provisional locks (PR #186, Owner 2026-10-20): "
               "`issue_53_cwnd_pushback` `video.render_fps` (`>= 14` under timing v1; restored to `>= 22` by #24) and "
               "`issue_120_rtx_payload_padding` `net.rtx_kbps >= 330` (was 380; libwebrtc 451, ours 344; tracked to #217; "
               "restored to 380 by #217's first-feedback timing, see 5.1). Provisional lock (PR #226, Owner 2026-10-22): "
               "`issue_5_adjuster_default` `cc.queue_over_200ms_pct < 30` (was `< 13`; ours 26.55 on seed 1 with #217's "
               "first-feedback timing and #272's call-start probe; 10-seed mean 13.84, libwebrtc 16.10, worst seed 36.0; "
               "tracked to #217).",
         verification="Covered by the 5.2 and 5.3 fixes. Pushback (issue #53, "
                      "[fix record](../../fixes/53-cwnd-pushback.md)): SendHistory counted packets of a lost "
                      "transport feedback as in flight for 60 s, which would hold the window shut; they are now "
                      "settled once a later feedback is reported. The library default is on (libwebrtc enables it "
                      "through the WebRTC-CongestionWindow field trial; rate_control_settings.cc, which holds the "
                      "default, is not in compare/webrtc/reference). Issue #446 "
                      "([fix record](../../fixes/446-cwnd-feedback-order.md)): after a transport feedback the pushback runs "
                      "only when the delay-based estimate updated, and before the window is resized from the target of the "
                      "last pushback update (`GoogCcNetworkController::OnTransportPacketsFeedback` and `OnProcessInterval`, "
                      "goog_cc_network_control.cc:215-241 and 421-541). The pushback used to resize the window first, so "
                      "the first feedback after an outage, which clears the lost packets from flight, ended the pushback "
                      "while the retransmissions still filled the bottleneck. Issue #464 "
                      "([fix record](../../fixes/464-int-bitrate-constraints.md)): libwebrtc's `BitrateConstraints` carry "
                      "int bps (`ConvertConstraints`, rtp_transport_controller_send.cc:68-83), so `cc::ConvertConstraints` "
                      "saturates a finite min, start or max rate at INT_MAX bps (about 2.1 Gbps) in `GccController`, "
                      "`rtp::SendSideController` and `rtp::Session`; an infinite max stays infinite. A start rate of 1 Tbps "
                      "or more had made the pacer build padding for probes at 3x and 6x that rate until it ran out of memory. "
                      "Issue #848: with the pushback on, `GoogCcNetworkController::OnSentPacket` runs "
                      "`MaybeTriggerOnNetworkChanged` for every sent packet (goog_cc_network_control.cc:263-287, "
                      "`RtpTransportControllerSend::ProcessSentPacket`), so a target that a receiver report, an RTT update or "
                      "an ALR change moved reaches the pacer, the probe controller and the allocator with the next sent packet; "
                      "ours now does the same (`GccController::MaybeTriggerOnSentPacket`), where it waited for the next "
                      "feedback or process interval."),
    dict(id="5.5", slug="pacer", title="Pacer with priority queues",
         problems=["Bursts filling router queues (bufferbloat) -> Pacer: smooths packet sends, with priority queues",
                   "Protecting audio over video -> audio prioritized in the pacer; video degrades first"],
         headers=["cc/pacer.h", "cc/pacing_controller.h"], unit=["Pacer", "PacingController", "PrioritizedPacketQueue", "PacketRouter"], e2e=["E2E_Pacer"],
         bench=["cc/Pacer_EnqueueProcess", "cc/Q_Pacer_audio_delay"],
         scenario="The closed-loop simulation at 2.5 Mbps with and without pacing; audio and video queueing "
                  "delay (p95) are compared.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "The pacer is a port of libwebrtc's PacingController and PrioritizedPacketQueue (M157, issue #70); the "
               "library's own media-debt pacer (`pacer v1`: 5 ms burst, drain on the oldest packet) was deleted in #167. "
               "It has a 40 ms "
               "burst interval (capped at 63 kB), media debt capped at 500 ms of the rate, 2 s cap on the time "
               "credited, a queue-time limit of 2 s judged on the average queue time, audio unpaced but "
               "accounted, queue levels audio / retransmission / video+FEC / padding, keep-alive padding every "
               "500 ms while paused or congested, and padding at the padding rate 5 ms at a time. The first "
               "packet of a keyframe flushes the queued video, FEC and retransmission packets, as "
               "`PacingController::EnqueuePacket` does (the sender marks keyframes and first-of-frame packets; "
               "#5). libwebrtc's flush also clears the RTX SSRC (`RemovePacketsForSsrc(rtx_ssrc)`, "
               "pacing_controller.cc:227-231), where `RTPSender::GeneratePadding` puts padding (the media SSRC without "
               "RTX), so queued padding goes too; ours does the same with `keyframe_flush_padding` (on by default "
               "as libwebrtc since #437's flip). Padding is made by `RtpSender::GeneratePadding` (`PacketRouter::GeneratePadding`, video streams asked before audio): RTX payload "
               "padding from the history first, then 255-byte empty padding packets on the RTX SSRC, each with its real size "
               "in the pacer queue; the keep-alive and the first packet of a probe ask for 1 byte (#120). Without RTX it goes on "
               "the media SSRC while `PacketSequencer::CanSendPaddingOnMediaSsrc` allows (video: after a frame's marker), audio "
               "padding sized to the request in [50, 255] B, with the padding fields set at release as `PacketSequencer` does "
               "(docs/fixes/514-padding.md). Remaining differences: round-robin between SSRCs of one level (one stream per kind in a "
               "one-to-one call), the packet TTLs (off by default), `SetCongested` is not driven by the "
               "congestion window (pushback stays on the encoder). `BitrateProber` (#48, `bitrate_prober.h`) times "
               "the probe bursts: a cluster starts when a media packet of at least min(`rate x min_probe_delta`, "
               "200 B) is enqueued, begins with a small padding packet, sends `rate x min_probe_delta` bytes per "
               "burst (padding fills it when there is no media), times the next burst by the bytes sent at the "
               "cluster rate and drops a cluster whose burst is more than 10 ms late (at most 5 pending, 5 s "
               "old). The pacer is also scheduled as `TaskQueuePacedSender` does (`Pacer::ProcessScheduled`, #123): a "
               "pass runs only when a packet was enqueued, a probe cluster was added, a rate or the congestion state changed or "
               "the pacer resumed while packets are queued (`PostMaybeProcessPackets`, #813), or the timer of the last "
               "pass is due, and only while `NextSendTime()` has come, with 5 ms (3 packets) between timer passes. Polling a bare "
               "`PacingController` every millisecond lets the early-return branch of `ProcessPackets` re-base its "
               "debt every call and sends queued packets between the audio packets instead of with them. The rates reach "
               "the pacer as `PacerConfig` carries them, whole bytes per 40 ms window (#813), and after the pushback "
               "target of the same GoogCC update (`GetPacingRates` in `MaybeTriggerOnNetworkChanged`).",
         verification="The unit tests port the intent of libwebrtc's pacing_controller_unittest.cc (burst "
                      "interval, debt cap, priorities, long-queue drain on the average queue time, padding, "
                      "congestion keep-alives). `PrioritizedPacketQueue` (#166) ports prioritized_packet_queue_unittest.cc: "
                      "12 of its 19 cases are ported and pass; the other 7 cover options that are off by default "
                      "(audio-retransmission priority, `fast_retransmissions`, the per-level TTLs) or the round robin "
                      "between SSRCs, which a one-to-one call does not need. The queue size that sets the long-queue drain "
                      "rate, the expected queue time (the encoder pause at 2 s) and GoogCC's process-interval pacer queue is "
                      "libwebrtc's `QueueSizeData`: payload and padding plus the transport overhead, without the RTP "
                      "headers (`PacedPacket::header_size`, #711; it was the wire size). "
                      "`PacketRouter` (#166) ports packet_router_unittest.cc against `rtp::SendSideController`, "
                      "`rtp::RtpSender` and `rtp::Session`, which share the router's work: padding with no stream, RTX "
                      "payload padding on the RTX SSRC, video preferred over audio for padding, RTP sequence numbers at "
                      "release, and transport-wide sequence numbers. The port found two differences, now fixed: "
                      "numbering starts at 1 (`transport_seq_(1)`), and a packet without the transport-cc "
                      "extension gets no number and is not reported to GCC (`NotifyBweOfPacedSentPacket`); neither "
                      "changes a scenario, which all map the extension. Not ported: RFC 8888 numbering "
                      "(`SetGenerateTransportSequenceNumbers`), `allow_sending` and ECT(1), which are off by default; "
                      "REMB and RTCP routing (`SendRemb`, `SendCombinedRtcpPacket`; the session's `RtcpEngine` sends "
                      "from its one local SSRC); module registration and its death tests, `PadsOnLastActiveMediaStream`, "
                      "`OnBatchComplete` and `GetRtxSsrcForMedia` between modules, which only matter with several send "
                      "streams of one kind (a one-to-one call has one audio and one video stream); and "
                      "`OnAbortedRetransmissions`, which M157's pacer never calls. "
                      "`TaskQueuePacedSender` (#166) ports task_queue_paced_sender_unittest.cc against `Pacer` with "
                      "its task-queue scheduling: 11 of its 14 cases in full and 3 in part, all passing (pacing over a second, "
                      "the burst window, rescheduling on a rate change, audio sent at once, the fixed and packet-based "
                      "coalescing windows, probes overriding them, probes 200 us apart, a probe ending in the send loop, "
                      "the oldest wait and the queue size). The port found two differences, now fixed: "
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
                      "With the 40 ms burst a frame leaves in one burst, so "
                      "E2E_Pacer expects that the paced run is not worse than the unpaced one on audio delay "
                      "(audio is not paced in libwebrtc either; v1's case was deleted with it in #167). The "
                      "pacer_v2_capacity_drop scenario keeps the v2 run reproducible; issue_5_keyframe_flush locks in "
                      "the keyframe flush (docs/fixes/5-pacer-keyframe-flush.md). Explained difference, not a gap "
                      "(issue #258, yardstick PR #275): at 15 fps on a clean 1.5 Mbps / 100 ms RTT link, audio "
                      "mouth-to-ear p95 and the queue p95 are about twice the 30 fps values. libwebrtc does the same "
                      "on the same link (seeds 1-10: mouth-to-ear p95 430 vs 446 ms, queue p95 110 vs 106 ms, "
                      "jitter-buffer p95 266 vs 280 ms; at 30 fps 235 vs 235, 44 vs 44, 132 vs 129). Each frame "
                      "is twice as large and leaves in one 40 ms burst, so the audio behind it waits longer at the "
                      "bottleneck and NetEq's target follows. `low_fps_clean_link` locks this in at libwebrtc's "
                      "levels. See "
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
               "the estimator ignores clusters with too few packets or bytes. They are libwebrtc's code (#48; the "
               "library's own probe controller and estimator, `cc v1`, were deleted in #167): "
               "`ProbeControllerV2` (`ProbeController`: clusters at 3x and 6x the start rate, further probes at 2x "
               "a result above 0.7 of the last probe, probes when the max or max allocated bitrate rises, "
               "`RequestProbe` after a big drop within 5 s in or just after an ALR, periodic ALR probes when "
               "a stream requests them, none while the estimate is limited by delay increase or loss), "
               "`ProbeBitrateEstimator` (receive/send ratio over 2 rejected, clusters expire "
               "after 1 s, the newest estimate wins), `AlrDetector` and `BitrateProber` (in the pacer). "
               "`AlrDetector` runs with the settings libwebrtc's does without field trials (#152): a budget of 0.80 x the "
               "estimate, ALR above 0.40 and out of it under -0.60, `WebRTC-ProbingScreenshareBwe`'s default "
               "`1.0,2875,80,40,-60,3` (`alr_experiment.cc`), which replaces `AlrDetectorConfig`'s 0.65/0.80/0.50; "
               "sm_compare read it from libwebrtc's live budget (PR #210). "
               "The loss-based state and the RTT limit decide the bandwidth-limited cause (#12, `GetBandwidthLimitedCause`). "
               "Remaining differences: no `NetworkStateEstimate`. "
               "The `GoogCcNetworkController` glue (#61): `GetPacingRates` (2.5 x the target until the first "
               "feedback, 1.1 x after, unless the streams set a pacing factor; padding rate), `StreamsConfig::pacing_factor` "
               "(#187: `rtp::Session` sets 1.1 when it adds a video stream with transport-cc, as `VideoSendStreamImpl`'s "
               "`GetConfiguredPacingFactor` does, so a video call paces at 1.1 x from the start), the packet-overhead rate off the video allocation "
               "(`RtpVideoSender::OnBitrateUpdated`), the post-encode overhead off the encoder target (#619: "
               "`GetPostEncodeOverhead`, the bytes packetization adds over the encoder output in the last second, at "
               "most half the target) and the congestion-window frame drop against the video target. With "
               "libwebrtc's default `CongestionWindowConfig` the window never reaches the pacer (`SetCongested` stays "
               "false), as in libwebrtc. "
               "Call start as libwebrtc's (#62): `rtp::Session` asks for periodic ALR probes when it adds an audio "
               "stream with transport-cc, as `AudioSendStream::ConfigureStream` does unless `WebRTC-Audio-AlrProbing` "
               "is disabled (`audio_send_stream.cc:158-159, 297-308`), and turns them off again when it then adds a "
               "video stream with transport-cc, as `VideoSendStreamImpl`'s constructor does with "
               "`RateControlSettings::UseAlrProbing()` (off without field trials; `video_send_stream_impl.cc:487-523`), "
               "so an audio-only call probes every 5 s in ALR and a call with video does not (#355: the ALR state and "
               "probe clusters of both stacks are in the bwe trace of yardstick #359; libwebrtc's only probes after the "
               "start are `RequestProbe`s); the streams' `max_total_allocated_bitrate` reaches `ProbeController` after the initial probes, "
               "because libwebrtc creates GoogCC when the network comes up, before the streams start "
               "(`rtp_transport_controller_send.cc:413-428, 747-773`), so an audio-only call's 3x and 6x probes are "
               "not capped at twice the audio allocation; and `BitrateProber` waits for a 200-byte RTP packet "
               "without the transport overhead (`PacingController::SetTransportOverhead`; `pacing_controller.cc:234`).",
         verification="No defects found. Ramp-up takes 0.2 s with probing; without it, 80% is not reached "
                      "within 30 s. Found in #62 from the `audio_only_lossy` traces on `parity-data`: libwebrtc's "
                      "first probe goes out at 3990 ms, with the first audio packet of 200 RTP bytes, and its "
                      "estimate stays at 300 kbps, while the library probed at 104 kbps at 10 ms and fell to 75-100 "
                      "kbps. Over seeds 1-10 the target goes 140 -> 209 kbps (libwebrtc 212), padding 30 -> 62 kbps "
                      "(61) and the queue maximum 6 -> 58 ms (45); `issue_62_audio_only_probing` locks it in."),
    dict(id="5.7", slug="bitrate-allocator", title="Bitrate allocation among streams",
         problems=["Sharing bandwidth among streams -> bitrate allocator, with per-stream priority and min/max bitrate"],
         headers=["cc/bitrate_allocator.h", "cc/bitrate_allocator_v2.h"], unit=["BitrateAllocator", "BitrateAllocatorV2"], e2e=["E2E_BitrateAllocator"],
         bench=["cc/BitrateAllocator_5streams", "cc/Q_Allocator_low_bandwidth"],
         scenario="Audio (enforced 20-40 kbps), camera (150-2500 kbps) and screen share (250-1500 kbps, priority "
                  "2) share a GCC target while capacity steps 3000 -> 450 -> 3000 kbps.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "Paused streams resume only at min x 1.1 (hysteresis). "
               "A `rtp::Session` runs the port of libwebrtc's `BitrateAllocator` (issue #104; the v1 split was deleted by #167) "
               "(`call/bitrate_allocator.cc`): `LowRateAllocation` / `NormalRateAllocation` / `MaxRateAllocation` with min + max(10 %, 20 kbps) "
               "and the protection share to resume, and the sum of the enforced minimums (audio, and video as `suspend_below_min_bitrate` is off) is "
               "the lower bound of GCC's target (`min_total_allocated_bitrate`, `use_min_allocatable_as_lower_bound_`). "
               "The video stream follows `VideoSendStreamImpl`'s encoder activity check (`video_send_stream_impl.cc`, `kEncoderTimeOut` 2 s; issue #375): "
               "with no frame encoded since the last check it leaves the allocator, and an encoder reconfiguration or the next encoded frame "
               "registers it again (a reconfiguration is posted to the worker queue, so a check due at the same instant runs first); "
               "each change of the limits reaches GoogCC as `OnStreamsConfig`, so a stream that comes back can start "
               "an allocation probe (`ProbeController::OnMaxTotalAllocatedBitrate`). "
               "Remaining: the elastic allocation (off by default in libwebrtc), the padding limit (`max_padding_rate`, with the prober, #48), "
               "the per-packet overhead libwebrtc takes off the video payload rate, and the audio min and max from Opus's frame-length range (not in `reference/`, #57).",
         verification="A unit-test expectation was wrong about how leftover bitrate is shared (it is split by "
                      "priority between audio and screen); the expectation was corrected. "
                      "Issue #375: at 1 fps with quality scaling, libwebrtc's start probe sends a third cluster at 2x the 300 kbps estimate "
                      "(an allocation probe after the video stream timed out and re-registered); with the activity check ours sends it too "
                      "(642 vs 627 kbps on `issue_335_fps1_keyframe_packets` seed 1). "
                      "With the check due at 2 s running before that instant's reconfiguration, the probe starts at 2 s, as libwebrtc's, "
                      "and its result comes after the probe controller's 1 s wait, so no further probe follows: the target ramps from 642 kbps, "
                      "and video kbps and utilization over seeds 1-10 read 932 and 51.6 % (libwebrtc 930 and 51.6 %; PR #388)."),
    dict(id="5.8", slug="frame-dropper", title="Encoder overshoot control (frame dropper)",
         problems=["Encoder bitrate overshoot -> encoder rate control, frame dropper"],
         headers=["cc/frame_dropper.h", "cc/encoder_bitrate_adjuster.h"], unit=["FrameDropper", "EncoderOvershootDetector", "EncoderBitrateAdjuster", "RateUtilizationTracker"], e2e=["E2E_FrameDropper"],
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
               "VideoSendStreamImpl::OnBitrateUpdated's (issue #326, `link_headroom on|off`, on): the encoder target "
               "less the protection, taken before the target is capped at the encoder maximum, so a video allocation "
               "above the maximum gives the adjuster headroom; below the maximum it equals the encoder target. The media utilization factor can come from a port of "
               "libwebrtc's RateUtilizationTracker (video/rate_utilization_tracker.cc; issue #355, "
               "`rate_utilization_tracker on|off`, on by default as libwebrtc since #355): produced bytes over the "
               "unadjusted target's budget, libwebrtc's default (WebRTC-BitrateAdjusterUseNewfangledHeadroomAdjustment); "
               "off, the factor comes from the detector, as with the field trial disabled. With `adjuster_on_update` (VideoSendControllerConfig, "
               "issue #569; on by default as libwebrtc since #637) the adjusted rate is "
               "recomputed when VideoStreamEncoder's UpdateBitrateAllocation runs: on each allocation update "
               "(OnBitrateUpdated), on a reconfiguration and every 1 s (kParameterUpdateIntervalMs), and held in "
               "between. An update runs it when it arrives (MaybeEncodePendingFrame, polled after the session ran, "
               "as OnBitrateUpdated; issue #894), so the overshoot detector's SetTargetRate leaks its buffers up to "
               "the update's time. Remaining differences: one stream with one temporal layer. A new frame size (quality scaling or CPU adaptation) resets both, as "
               "VideoStreamEncoder::ReconfigureEncoder (issue #346): FrameDropper::Reset and SetRates with the encoder "
               "target, and the adjuster starts again at kDefaultUtilizationFactor 1.2 for kMinFramesSinceLayoutChange "
               "30 frames. The VideoStreamEncoder has no keyframe budget of its own: a keyframe "
               "request only sets next_frame_types_ (sticky until a keyframe is encoded, as here) and the keyframe "
               "then goes through the frame dropper like any frame.",
         verification="Issue #53 ([fix record](../../fixes/53-cwnd-pushback.md)): keyframe bytes entered an uncapped "
                      "bucket and a run of keyframe requests made the dropper drop every delta frame for seconds. "
                      "Issue #5 ([fix record](../../fixes/5-frame-dropper-port.md)): replaced by the libwebrtc "
                      "port (3 s cap, rescale on a rate drop); the 1.5 s cap and the keyframe exemption are gone. "
                      "Issue #326: the link allocation was the capped encoder target, so above the 2.5 Mbps maximum "
                      "the adjuster had no headroom and gave the encoder 2266-2270 kbps where libwebrtc gives "
                      "2404-2466 (issue_326_congestive_sync seeds 3 and 5, t = 3 s, parity-data-pr/pr/327); the "
                      "protection returned to the allocator also came from the capped target. Both now follow "
                      "rtp_video_sender.cc:885-939 and video_send_stream_impl.cc:902-914. "
                      "Issue #346 ([fix record](../../fixes/346-reconfigure-encoder-reset.md)): a resolution switch kept "
                      "the adjuster's learned factor (1.03-1.11) and the dropper's state, so the switch keyframes were "
                      "1.2x libwebrtc's (41 vs 34 kB at 2.5 Mbps); both are now reset as "
                      "video_stream_encoder.cc:1430-1442 does. "
                      "Issue #355: after a reset the encoder target came back to 2228-2317 kbps where libwebrtc's reaches "
                      "the 2.5 Mbps cap within a second, because the media factor came from the overshoot detector (sized "
                      "against the adjusted rate, never below 1.0) instead of RateUtilizationTracker (against the unadjusted "
                      "target). With `rate_utilization_tracker on` the encoded rate over issue_346_reconfigure_cross_join's "
                      "first 10 s is 2328 kbps against libwebrtc's 2321 (2221 without), scenario issue_355_rate_tracker_ramp. "
                      "Issue #466: the shared encoder model has persistent overshoot/undershoot (`encoder_overshoot`) "
                      "and scene-cut (`scene_change`) modes, off by default, so these ports meet a real rate miss on "
                      "both stacks of `sm_compare` (scenarios issue_466_*; `video.encoder_target_avg_kbps` and "
                      "`video.encoder_dropped` are shown next to libwebrtc's). At 1.3x on a 1 Mbps link the adjuster "
                      "lowers the encoder target from 788-800 to 625-637 kbps (seeds 1-3). "
                      "Issue #894: an allocation update reached the adjuster at the next captured frame (BWE updates at "
                      "130/180/230/280/430 ms applied at 133/200/233/300/433 ms), so the detector leaked 3-20 ms at the "
                      "old rate; at the first detector-based update (1133 ms) the link factor was 1.2381 and the encoder "
                      "got 1839899 bps where libwebrtc's frame implies about 1842 kbps. Run when the update arrives, "
                      "issue_24_timing_v2_default seed 1 matches libwebrtc's forward link through row 570 (2.5 s) "
                      "instead of row 229 (1.133 s); unit test "
                      "`VideoSendController.BitrateAdjusterRunsWhenTheAllocationUpdateArrives`."),
    dict(id="5.9", slug="video-adaptation", title="Resolution/frame-rate adaptation and CPU overuse detection",
         problems=["Low bandwidth -> adaptive resolution / framerate degradation (degradationPreference)",
                   "CPU too slow to encode -> CPU overuse detector, which lowers resolution or fps"],
         headers=["cc/video_adaptation.h"], unit=["OveruseFrameDetector", "BitrateConstraint"], e2e=["E2E_VideoAdaptation"],
         bench=["cc/CpuOveruseDetector_PerFrame"],
         scenario="(a) A slow device (40 ms of encode per megapixel) through libwebrtc's chain (OveruseFrameDetector, "
                  "ResourceAdaptationProcessor, VideoStreamAdapter, VideoAdapter): the CPU detector must settle on a "
                  "sustainable resolution without oscillating. (b) With the encoder's resolution bitrate limits, the "
                  "step back up to 720p waits until the encoder target reaches 720p's min_start_bitrate "
                  "(BitrateConstraint); without them it does not wait.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "The adaptation itself is libwebrtc's, in `video::VideoStreamAdapter` and "
               "`video::ResourceAdaptationProcessor` (7.8), driven by `rtp::VideoSendController`. "
               "Issue #819 removed the library's own bits-per-pixel ladder (`DegradationController`, "
               "0.04/0.06 bpp hysteresis) and the exponentially filtered v1 CPU detector "
               "(`CpuOveruseConfig::version = 1`): neither has a libwebrtc counterpart, and no call path used them.",
         verification="No defects found after the 5.2 fixes. `CpuOveruseConfig::version = 2` (issue #84 step 3; the default since #579) "
                      "is libwebrtc M157's `OveruseFrameDetector` (`video/adaptation/overuse_frame_detector.cc`) "
                      "with its default estimator `SendProcessingUsage1` and the `CpuOveruseOptions` a software "
                      "encoder gets (85% / 42%, 120 frame samples, 3 process counts, 2 checks in a row, 1.5 s frame "
                      "time-out, first check 100 ms after start then every 5 s, ramp-up delay 10 s quick, 40 s "
                      "standard, doubled up to 240 s). The `OveruseFrameDetector` unit tests port "
                      "`overuse_frame_detector_unittest.cc` (all but the two random-interval cases and the "
                      "`SendProcessingUsage2` suite, which is off by default). Issue #84 step 5 wires version 2 into "
                      "the send path as libwebrtc's `EncodeUsageResource` "
                      "(`video/adaptation/encode_usage_resource.cc`), behind `quality_scaling` with the quality "
                      "scaler: `rtp::VideoSendController` starts each encoded frame in `OnCapturedFrame` and "
                      "completes it in `OnEncoded`, overuse adapts the resolution down and underuse up, and the "
                      "target frame rate is the codec maximum. Scenario key `encode_delay on` makes the encode "
                      "time real on both stacks (the test bench delays the frame; `sm_compare`'s model encoder "
                      "posts its encoded callback that much later). `issue_84_cpu_overuse_slow_encoder` locks in "
                      "the behaviour. Issue #819: `BitrateConstraint` (`video/adaptation/bitrate_constraint.cc`) is ported into "
                      "`video::VideoStreamAdapter::GetAdaptationUp` with `EncoderInfo::GetEncoderBitrateLimitsForResolution`; "
                      "`rtp::VideoSendController` gives it the start bitrate and every nonzero encoder target "
                      "(`VideoStreamEncoderResourceManager::SetStartBitrate` / `SetTargetBitrate`) and "
                      "`VideoSendControllerConfig::resolution_bitrate_limits`, empty by default as libwebrtc's software "
                      "encoders and both stacks' encoder model in `sm_compare` report, so it allows every step and no "
                      "scenario moves. Unit tests `BitrateConstraint.*` port `bitrate_constraint_unittest.cc`'s "
                      "singlecast cases (simulcast and SVC are not ported). The bits-per-pixel `DegradationController` "
                      "and CPU detector v1 were removed, with their tests (`VideoAdaptation.*`, the closed-loop "
                      "`E2E_VideoAdaptation.ResolutionFollowsBandwidth`, `Q_Adaptation_bandwidth_drop`); "
                      "`E2E_VideoAdaptation` now runs libwebrtc's chain (`tests/e2e/video_adaptation_e2e_test.cc`)."),
    dict(id="5.11", slug="outage", title="Link outage detection and recovery",
         problems=["Link outage, then recovery -> encoder pause, then keyframe on recovery; ICE restart if needed"],
         headers=["cc/outage_monitor.h"], unit=["Outage"], e2e=["E2E_Outage"],
         bench=["cc/Q_Outage_recovery"],
         scenario="A 2 Mbps call with a 7 s total outage (both directions) at 20 s; packets sent into the dead "
                  "link, ICE restarts and time to recover 50% of capacity are measured.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#5-congestion-control) for the differences from libwebrtc. "
               "The monitor proposes an encoder pause, an ICE restart request, and on recovery a keyframe and an "
               "estimate reset to half the last good rate. libwebrtc has no outage monitor, so the "
               "`SendSideController` runs it only to follow the link (state, `sender.outage.*` metrics, `outage` "
               "events) and acts on none of them (issues #151, #849; only `cc v1` did, deleted in #167): the encoder keeps running, the congestion "
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
                      "and the audio gap on all 10 match libwebrtc. Explained difference (Owner decision on issue "
                      "#151, 2026-10-20): with every v2 switch on, `outage_3s` `cc.queue_p95_ms` stays above "
                      "libwebrtc's (`issue_151_outage_3s_v2`, seeds 1-10 on main 0b45569: mean 64.2 vs 40.6 ms, "
                      "sd 32.1 vs 4.0) while freeze total (3413 vs 3227 ms), repair (1.0 vs 0.4 %) and the longest "
                      "audio gap (2980 vs 2977 ms) are in band. Every input to the recovery matches libwebrtc: "
                      "in-flight accounting, overhead rate (#192), link-change timing (#190), the 300 ms "
                      "keyframe-request interval (#195), no outage monitor and the tick order (#184), and "
                      "`NackRequester::ClearUpTo` (#172). What remains is a phase race over which frame the "
                      "congestion window drops first after `up`, which decides whether the NACK lands before, in or "
                      "after the keyframe's tick; the mean swings with unrelated small inputs (50.5 -> 66.2 -> "
                      "64.2 ms across #198, #199 and #202). Seeds 2 and 3 stay tracked to #24, as above. Issue #849: "
                      "nothing called `OutageMonitor::Process` on the call path, so its state stayed `up` and "
                      "`sender.outage.outages` at 0 through every outage; `SendSideController::Process` now runs it "
                      "and discards its actions (`SendSideController.OutageMonitorFollowsTheLinkAndTakesNoAction`). "
                      "Every scenario's call metrics are unchanged; only the `outage` event counts appear."),
    # ------------------------------------------------------------------ 6
    dict(id="6.1", slug="echo-canceller", title="Acoustic echo cancellation",
         problems=["Echo (far-end audio coming back through the mic) -> AEC3 acoustic echo canceller; AECM for mobile"],
         headers=["audio/aec3_filter.h", "audio/aec3_erle.h", "audio/aec3_reverb.h",
                  "audio/aec3_analysis.h", "audio/aec3_state.h", "audio/aec3_suppression.h", "audio/aec3.h"],
         unit=["Aec3Filter", "Aec3Erle", "Aec3Reverb", "Aec3Analysis", "Aec3State", "Aec3Suppression",
               "Aec3"],
         e2e=["E2E_EchoCanceller"],
         bench=["audio/Q_EchoCanceller_full_duplex_call"],
         scenario="A 30 s two-party call: far-end speech played into a simulated room (40 ms response, RT60 "
                  "100 ms, 60 ms bulk delay), far end alone for 15 s, then double talk. ERLE is measured while "
                  "only the far end talks, and near-end preservation during double talk, through HPF -> AEC -> NS.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The capture chain runs libwebrtc's AEC3 (`audio/aec3.h`); the library's own PBFDAF canceller (v1) was retired in #565. "
               "AEC3's linear stage (issue #240 step 2, `audio/aec3_filter.h`) is a port of libwebrtc M157's "
               "`aec3_fft`, the render buffers (`block_buffer`, `spectrum_buffer`, `fft_buffer`, `render_buffer` and the "
               "whole `render_delay_buffer`, of which 6.2's low-rate buffer is the decimated half), `adaptive_fir_filter` "
               "(partitioned frequency-domain filter, 64-sample partitions, one partition constrained per block), "
               "`adaptive_fir_filter_erl`, `render_signal_analyzer`, `refined_filter_update_gain` (NLMS with the "
               "filter-error estimate H_error and ERL leakage), `coarse_filter_update_gain` and `subtractor` (refined and "
               "coarse filters, misadjustment rescaling, coarse filter reset from the refined one), with "
               "EchoCanceller3Config::Filter's defaults: 12 then 13 partitions, leakage 0.005/0.5 then 0.00005/0.05, error "
               "floor 0.001 and ceiling 2, noise gate 20075344, coarse rate 0.9 then 0.7, 250-block configuration "
               "changes, a 25-block hangover after a coarse filter reset. The FFT is libwebrtc's 128-point `OouraFft` "
               "(`audio/ooura_fft.h`, from `common_audio/third_party/ooura/fft_size_128`, its C path and stored tables; "
               "#240 step 6). Remaining differences: the portable C paths where x86 libwebrtc runs SSE2/AVX2; several render "
               "or capture channels feed the delay estimator through `alignment_mixer` (adaptive selection of the "
               "strongest channel, M157's default; #565); Subtractor takes "
               "AecState's capture saturation flag as an argument. "
               "AEC3's echo state (#240 step 3, `audio/aec3_state.h` with `aec3_erle.h`, `aec3_reverb.h` and "
               "`aec3_analysis.h`) ports `aec_state` (linear filter usability after 0.4 s of active unsaturated render, "
               "0.2 s after a reset; initial state for 2.5 s; direct-path filter delay; echo saturation), "
               "`erl_estimator`, `erle_estimator` with its fullband, subband (max 4 below and 1.5 above bin 32, onset "
               "detection) and signal-dependent parts, `reverb_model`, `reverb_decay_estimator`, "
               "`reverb_frequency_response`, `reverb_model_estimator`, `filter_analyzer`, `echo_audibility`, "
               "`stationarity_estimator`, `transparent_mode` (the legacy classifier, M157's default; the HMM one through "
               "`TransparentMode::CreateHmm()` in place of the WebRTC-Aec3TransparentModeHmm trial) and "
               "`subtractor_output_analyzer`, with EchoCanceller3Config's Erle, EpStrength and EchoAudibility "
               "defaults. Field trials are read as not set. "
               "AEC3's residual echo suppression (#240 step 4, `audio/aec3_suppression.h`) ports "
               "`residual_echo_estimator` (linear S2 / ERLE, or the render power around the delay times the echo "
               "path gain, with the render noise floor and soft noise gate, plus the reverb tail), "
               "`suppression_gain` (echo-to-nearend and echo-to-noise masking thresholds per bin, gain rise and fall "
               "limits, high-frequency and upper-band limits, the low-noise render detector), "
               "`dominant_nearend_detector`, `subband_nearend_detector`, `moving_average_spectrum`, "
               "`suppression_filter` (sqrt-Hanning overlap-add with comfort noise, upper bands delayed one block) "
               "and `comfort_noise_generator`, with EchoCanceller3Config's Suppressor, ComfortNoise, EchoModel and "
               "EpStrength defaults. Remaining differences: no neural residual echo estimator (optional in "
               "libwebrtc and absent in a call); the Low{Early,Late}ReflectionsDefaultGain and "
               "ErleOnsetCompensationInDominantNearend field trials are read as not set. "
               "AEC3 as a whole (#240 step 5, `audio/aec3.h`) ports `echo_remover` (refined or coarse filter output "
               "with a 30-sample crossfade, sqrt-Hanning spectra of the capture and the linear output, linear echo "
               "|Y - E|^2, then AecState, comfort noise, residual echo, suppression gain and filter), "
               "`echo_remover_metrics`, `block_processor` (render start, buffer flush at overrun, delay controller "
               "reset at underrun, render buffer alignment to the estimated delay), `frame_blocker`, `block_framer`, "
               "`block_delay_buffer` and `echo_canceller3` (render queue of 100 frames, capture saturation at 32700, "
               "10 ms frames cut into 64-sample blocks), and it runs in the `apm v2` chain as APM calls it. On #63's "
               "signals it gives libwebrtc M157's numbers: ERLE 33.2 / 50.1 / 8.0 / 49.0 dB on the route change, "
               "67.0 dB with NS, near end -5.3 dB and SNR -0.2 dB in double talk. Multichannel render (#565) "
               "ports `multi_channel_content_detector` (proper stereo after more than 2 s of frames whose channels "
               "differ, back to mono after 300 s without; the channels' average in the meantime, flagged as an echo "
               "path change), `config_selector` and `CreateDefaultMultichannelConfig` (coarse filter 11 blocks at "
               "rate 0.95, suppressor max increase 1.5 and low-frequency decrease 0.35): on proper stereo AEC3 is "
               "re-created on every render channel with that config, as APM's default. Remaining differences: "
               "AdjustConfig's field "
               "trials are read as not set; no linear AEC output export (off by default) and no UMA histograms; "
               "the render queue is a FIFO, not a thread hand-off.",
         verification="Several defects were found and fixed. (1) The delay estimate was wrong (see 6.2), which "
                      "capped ERLE at 12 dB. (2) Double talk corrupted the filter: the background filter partly "
                      "'cancels' near speech for a few blocks and was then copied to the foreground. An "
                      "ERLE-based double-talk detector was tried and rejected (it fired on 40% of far-end-only "
                      "blocks). The final design uses Speex's statistical copy rule plus Valin's adaptive step "
                      "size, which collapses during double talk (step 0.35 -> 0.002): near-end SNR went from "
                      "-4 to +9 dB. (3) After an echo-path change the convergence state was not reset, so the "
                      "adaptive step stayed near zero and ERLE never recovered (0 dB); it now recovers to "
                      "25.6 dB. AEC3's linear stage (#240 step 2): libwebrtc's aec3_fft, render_delay_buffer, "
                      "adaptive_fir_filter (FilterSize, FilterStatisticsAccess, FilterAndAdapt), "
                      "coarse_filter_update_gain, refined_filter_update_gain and subtractor unit tests pass with their "
                      "signals and bounds (multi-channel cases with the debug-build parameters; the SIMD-equivalence and "
                      "death tests don't apply). The first FFT used fft4g's twiddles, built with cosf; the round trip of "
                      "aec3_fft_unittest's FftAndIfft was 0.002 off at 2.6e6 against its 0.001 bound, so the 128-point "
                      "tables are now built in double precision as OouraFft stores them, and it passes. AEC3's echo state "
                      "(#240 step 3): libwebrtc's aec_state (NormalUsage for 1, 2 and 8 render and capture channels, "
                      "ConvergedFilterDelay), erl_estimator, erle_estimator (VerifyErleIncreaseAndHold, "
                      "VerifyErleTrackingOnOnsets), signal_dependent_erle_estimator, reverb_model_estimator and "
                      "filter_analyzer (FilterResize) unit tests pass with their signals and bounds; echo_audibility, "
                      "transparent_mode and subtractor_output_analyzer, which have no libwebrtc unit tests, have "
                      "behavioural tests derived from the code. AEC3's residual echo suppression (#240 step 4): "
                      "libwebrtc's moving_average_spectrum, comfort_noise_generator (CorrectLevel), suppression_gain "
                      "(BasicGainComputation, UpdateStateDependingOnConfig), suppression_filter (ComfortNoiseInUnityGain, "
                      "SignalSuppression, SignalTransparency, Delay) and residual_echo_estimator (BasicTest for 1, 2 and "
                      "4 render and capture channels, ReverbTest) unit tests pass with their signals and bounds; the "
                      "death tests and the neural estimator's metrics tests don't apply. The two nearend detectors, "
                      "which have no libwebrtc unit tests, have behavioural tests derived from the code. AEC3 as a "
                      "whole (#240 step 5): libwebrtc's frame_blocker, block_framer, block_delay_buffer, "
                      "echo_remover_metrics, echo_remover (BasicApiCalls, BasicEchoRemoval) and block_processor "
                      "(BasicSetupAndApiCalls, TestLongerCall) unit tests pass with their signals and bounds; "
                      "EchoCanceller3's pass-through, render queue and saturation tests are derived from "
                      "echo_canceller3_unittest.cc on the real block processor (the originals mock it). "
                      "E2E_EchoDelay.Aec3FollowsAudioRouteChangeAsLibwebrtc and "
                      "E2E_EchoCanceller.Aec3FullDuplexCallAsLibwebrtc check the `apm v2` chain against libwebrtc "
                      "M157's APM on the same signals (docs/results/apm-webrtc.md) within 1 dB. #240 step 6 moved Aec3Fft from "
                      "fft4g's rdft to the ported `OouraFft` (fft_size_128): Aec3Filter.OouraFft128MatchesDft checks it "
                      "against a double-precision DFT, the aec3_fft unit tests still pass, and the e2e ERLE and double-talk "
                      "numbers stay at libwebrtc's."),
    dict(id="6.2", slug="echo-delay", title="Echo-path delay estimation",
         problems=["Delay between the speaker and mic paths -> echo-path delay estimation in AEC3"],
         headers=["audio/aec3_delay.h"], unit=["Aec3Delay"], e2e=["E2E_EchoDelay"],
         bench=["audio/Q_EchoDelay_route_change"],
         scenario="A far-end-only call where the echo delay jumps from 60 to 180 ms at 12 s (speaker -> "
                  "Bluetooth). The AEC must re-estimate the delay and re-converge.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The capture chain runs AEC3's own delay estimation (`audio/aec3_delay.h`); the library's GCC-PHAT estimator (v1) was retired in #565. "
               "AEC3's delay estimation (issue #240 step 1, `audio/aec3_delay.h`) is a port of libwebrtc M157's "
               "decimator, matched filter, lag aggregator, clock-drift detector, echo-path delay estimator, render delay "
               "controller and the low-rate half of the render delay buffer, with EchoCanceller3Config's delay defaults "
               "(5 filters of 32 sub-blocks at 4 kHz covering 0-552 ms, headroom 32 samples, thresholds 5/20, pre-echo "
               "detection on), and `alignment_mixer`, which makes several render and capture channels mono for it "
               "(adaptive selection of the strongest channel, M157's default; #565). It runs inside EchoCanceller3. "
               "Remaining differences: the portable C matched-filter core where x86 libwebrtc runs SSE2/AVX2 (float "
               "summation order only); no UMA metrics or data dumps.",
         verification="Defect found: with rectangular windows, the window edges correlated with each other and "
                      "produced false peaks at both ends of the lag range (0 and 8000 samples). Fixed with "
                      "Hann/Tukey tapering and a partial phase transform (|C|^0.8). The synthetic room response "
                      "was also made physical (direct path dominant). AEC3 delay estimation (#240): the mono cases of "
                      "libwebrtc's decimator, matched_filter, matched_filter_lag_aggregator, clockdrift_detector and "
                      "echo_path_delay_estimator unit tests pass (both down-sampling factors, with and without pre-echo "
                      "detection; the SIMD-equivalence and death tests don't apply). On speech through a room response "
                      "with an echo path of 60 then 180 ms, the render delay controller reports 832 and 2752 samples "
                      "(true 968 and 2888 minus the 32-sample headroom, one pre-echo block early) with a refined "
                      "estimate and no clock drift; the first estimate comes after 176 ms."),
    dict(id="6.3", slug="noise-suppression", title="Noise suppression",
         problems=["Background noise -> NS noise suppression (RNNoise-style ML in some builds)"],
         headers=["audio/ns.h"], unit=["Ns"], e2e=["E2E_NoiseSuppressor"],
         bench=[],
         scenario="Near-end speech in white noise at about 7 dB SNR through HPF -> NS; SNR against the clean "
                  "talker (passed through the same HPF) before and after.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The capture chain runs libwebrtc's NS (`audio/ns.h`, kHigh); the library's STFT-Wiener suppressor (v1) was retired in #565. "
               "`ns::NoiseSuppressor` (#241) ports libwebrtc M157's "
               "NS (`modules/audio_processing/ns/`: quantile noise estimator with the parametric white/pink model "
               "during start-up, LRT, spectral flatness and spectral difference features with histogram-learnt "
               "thresholds, speech probability, decision-directed Wiener filter with the attenuation adjustment, "
               "160-sample frames in a 256-point hybrid Hann/flat filter bank, one gain for the upper bands) at "
               "kHigh (18 dB floor, over-subtraction 1.1), the level `WebRtcVoiceEngine::ApplyOptions` sets. It "
               "analyzes the capture before the AEC and suppresses after it, as `AudioProcessingImpl` does. "
               "The 256-point FFT is Ooura's `fft4g` as `NrFft` runs it (`sm/audio/fft4g.h`, from "
               "`common_audio/third_party/ooura/fft_size_256/`), bit-exact with libwebrtc's `WebRtc_rdft` "
               "(#241). Several channels are handled as libwebrtc does: per-channel estimators, one Wiener filter "
               "that is the minimum over the channels per bin, and the minimum overall scaling factor and upper-band "
               "gain (#241). No difference from libwebrtc's NS remains.",
         verification="On the v1 suppressor (retired in #565): the first chain test measured SNR against the raw "
                      "talker, so the HPF's low-frequency phase shift counted as noise; the reference then went through "
                      "the same HPF, and v1 raised SNR from 7.6 to 12.9 dB. libwebrtc's NS (`apm v2`, #241) on the e2e noise case, measured as "
                      "`sm_apm_compare` measures both stacks: SNR improvement -0.2 dB (libwebrtc -0.1, v1 +5.3), "
                      "noise attenuation in pauses 17.9 dB (17.9, v1 16.9), level change in speech -3.2 dB (-2.9, "
                      "v1 -1.3), delay 6 ms (6, v1 10). Like libwebrtc's, it takes the speech down along with the "
                      "noise in speech frames, so the SNR against the clean talker does not rise. With `fft4g` in "
                      "place of `RealFft` (#241) the same four values hold; unit `Ns.Fft4gBitExactWithLibwebrtc` "
                      "pins the transform to the output of libwebrtc M157's prebuilt `WebRtc_rdft`. The whole "
                      "suppressor was then run side by side with libwebrtc M157's prebuilt `NoiseSuppressor`, "
                      "driven through its `AudioBuffer` on the same input: the output is identical bit for bit at "
                      "16, 32 and 48 kHz with one, two and three channels (#241); unit "
                      "`Ns.BitExactWithLibwebrtcMultichannel` pins it. So the 0.3 dB left in the speech level above "
                      "comes from the chain in front of NS (this library's high-pass filter, #239), not from NS."),
    dict(id="6.4", slug="gain-control", title="Automatic gain control and limiter",
         problems=["Volume too quiet, too loud, or clipping -> AGC2 automatic gain control, limiter, clipping predictor"],
         headers=["audio/agc2.h", "audio/agc2_rnn_vad.h", "audio/sinc_resampler.h", "audio/agc1.h", "audio/agc1_vad.h", "audio/agc_manager.h", "audio/clipping_predictor.h"], unit=["GainController2", "Agc2RnnVad", "Agc1", "Agc1VoiceActivity", "Agc1Manager", "Agc1CaptureLevels", "Agc1ClippingPredictor", "Agc1ClippingLevelBuffer"],
         e2e=["E2E_GainControl"],
         bench=[],
         scenario="A quiet talker (~-33 dBFS) for 20 s, then a loud one (+20 dB), through AGC1 as a call runs it "
                  "and through AGC2; the quiet and loud talkers' levels, the noise in pauses and the peak are "
                  "compared with libwebrtc's on the same signal.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The capture chain's default is `apm v2` (#565): libwebrtc's AGC1 (`AgcManagerDirect` with the emulated mic volume, as a call runs it; AGC2 on request, as libwebrtc M157's call never enables `gain_controller2`, #670) runs; the library's own AGC and limiter (v1) were retired in #565. "
               "`agc2::GainController2` (#242) ports libwebrtc M157's AGC2 "
               "(`modules/audio_processing/agc2/`: adaptive digital gain, speech and noise level "
               "estimators, saturation protector, limiter with its interpolated gain curve) with its "
               "defaults (headroom 5 dB, max gain 50 dB, initial gain 15 dB, 6 dB/s, noise cap -50 dBFS). "
               "The speech probability comes from AGC2's RNN VAD, ported in `agc2_rnn_vad.h` "
               "(`vad_wrapper.cc` and `rnn_vad/`: 24 kHz features from the LP residual's pitch and the "
               "Opus-band cepstrum, FC-GRU-FC network with the rnnoise weights, reset every 1500 ms, the "
               "capture resampled with libwebrtc's `PushSincResampler`), and `GainController2` follows "
               "`gain_controller2.cc`'s order (fixed gain after the adaptive gain, before the limiter). Its "
               "dot products sum in order (libwebrtc's portable path) and its FFTs are ours, not PFFFT, so "
               "the probability matches libwebrtc to float precision, not bit for bit. AGC1, the call "
               "default: `agc1::GainControl` ports its digital compression stage, which every call runs "
               "(`AgcManagerDirect::SetupDigitalGainControl` puts `GainControlImpl` in `kFixedDigital` with "
               "a 2 dB target, 7 dB compression and the limiter): `agc/legacy/digital_agc.cc` (gain table, "
               "envelope followers, gate, overload limit, the AGC's VAD), the fixed-digital parts of "
               "`agc/legacy/analog_agc.cc` and `gain_control_impl.cc`. `agc1_vad.h` ports `Agc`, the loudness "
               "meter `AgcManagerDirect` reads (`agc/agc.cc`, `loudness_histogram.cc`, `utility.cc`), with "
               "its `VoiceActivityDetector` (`vad/`: the WebRtcVad GMM VAD of `common_audio/vad/`, the "
               "pitch-based VAD on iSAC's pitch estimator and ooura's fft4g); it takes 16 kHz 10 ms chunks "
               "only, as `AgcManagerDirect` feeds it split band 0, so the resampler is left out. `agc_manager.h` "
               "ports `AgcManagerDirect` / `MonoAgc` (`agc/agc_manager_direct.cc`: compression gain 2-12 dB "
               "from 7 in 0.05 dB steps, the analog volume 12-255 through `kGainMap` with at most 15 dB per "
               "update, clipping handling with libwebrtc's `AnalogGainController` defaults) and APM's analog "
               "mic gain emulation (`capture_levels_adjuster/`), the test bench's modelled mic volume: the "
               "capture is scaled by volume / 255 from 255, ramped over the frame. The `apm v2` e2e chain now "
               "runs AGC1 as a call does, in `AudioProcessingImpl`'s order. `clipping_predictor.h` ports "
               "AGC1's clipping predictor (`agc2/clipping_predictor.cc`, `clipping_predictor_level_buffer.cc`: "
               "event prediction from a crest factor drop, or peak prediction with a fixed or adaptive step) "
               "and `AgcManagerDirect` asks it for a step after the clipping wait, as libwebrtc does; it is off "
               "by default (`ClippingPredictor::enabled = false`), as in libwebrtc, so a call never creates it "
               "and no metric moves.",
         verification="On the v1 AGC (retired in #565): the first e2e expectations contradicted the AGC2 design "
                      "(noise cap, no attenuation) and were rewritten to test those behaviours, and a unit test reused "
                      "a buffer that the limiter had already modified in place. AGC2 (`apm v2`, #242) on the e2e quiet-then-loud talker: "
                      "quiet talker -21.1 dBFS (libwebrtc AGC2 -21.0, v1 -16.8), loud talker -1.2 dB (-1.0), "
                      "noise in pauses -62.7 dBFS (-62.9), peak 0 dBFS (0.0). libwebrtc's "
                      "`GainDoesNotChangeFast` unit test never runs its loops (its frame count is negative); "
                      "the port runs them. AGC1's digital stage (#242) is bit-exact with libwebrtc's "
                      "`digital_agc.cc` and `analog_agc.cc` compiled from `reference/` and driven as "
                      "`GainControlImpl` drives them: the gain tables for four configurations, and the 11 "
                      "per-millisecond gains and the S16 output of every frame over 30 s of talk spurts at "
                      "-45, -25 and -8 dBFS (`Agc1.GainTableMatchesLibwebrtc`, "
                      "`Agc1.GainsAndOutputMatchLibwebrtc`). `Agc` and its VAD are bit-exact with the prebuilt M157 "
                      "`libwebrtc.a` on x86-64 glibc (WebRtcVad decisions in four configurations, every chunkwise voice probability "
                      "and RMS, and every `GetRmsErrorDb` update over 30 s; `Agc1VoiceActivity.*`); elsewhere libm "
                      "may round differently, so the test checks to a tolerance there. The first goldens came from the "
                      "reference files compiled with libstdc++, where fft4g's `cos(float)` is the double function; "
                      "libwebrtc's libc++ picks the float overload, which the Reviewer caught and the library confirmed. The first port used SPL's unsigned "
                      "`WEBRTC_SPL_SCALEDIFF32` for the envelope followers; `digital_agc.cc` has its own "
                      "signed `AGC_SCALEDIFF32`, and the golden test caught the difference. `AgcManagerDirect` (#242) "
                      "passes the ports of libwebrtc's `agc_manager_direct_unittest.cc` (`Agc1Manager.*`, with "
                      "its mic volume and compression gain sequences, in the six parametrizations), and AGC1 "
                      "in the `apm v2` chain gives on the e2e quiet-then-loud talker: quiet talker -20.4 dBFS "
                      "(libwebrtc AGC1 -20.6, v1 -16.8), loud talker -3.3 dB (-3.2), noise in pauses "
                      "-62.6 dBFS (-63.0), peak 0 dBFS (0.0). AGC2's RNN VAD (#242) passes the ports of libwebrtc's "
                      "`rnn_fc`, `rnn_gru`, `spectral_features(_internal)`, `features_extraction` and `vad_wrapper` "
                      "unit tests with their test vectors and tolerances, and `PushSincResampler` the chirp "
                      "error thresholds of `push_sinc_resampler_unittest.cc` (`Agc2RnnVad.*`); the tests that read "
                      "libwebrtc's resource files are not ported (the files are not in the reference sources). "
                      "On the test bench's speech-like signal 87% of frames reach a probability of 0.95, on white "
                      "noise none. With it, AGC2 gives on the e2e quiet-then-loud talker: quiet talker -21.05 dBFS "
                      "(libwebrtc AGC2 -21.0; -21.10 with our VAD's decision), loud talker -1.2 dB (-1.0), noise "
                      "in pauses -62.6 dBFS (-62.9). The clipping predictor (#242) passes the ports of libwebrtc's "
                      "`clipping_predictor_unittest.cc` and `clipping_predictor_level_buffer_unittest.cc` with "
                      "their parametrizations (`Agc1ClippingPredictor.*`, `Agc1ClippingLevelBuffer.*`: adaptive "
                      "step 17, fixed step 15), and `AgcManagerDirect` with it the predictor tests of "
                      "`agc_manager_direct_unittest.cc` (used predictions lower the volume 255 -> 240 -> 225 "
                      "before clipping, unused ones leave it as without the predictor); with the default config "
                      "the e2e chain is unchanged."),
    dict(id="6.5", slug="high-pass-filter", title="High-pass filter",
         problems=["Low-frequency rumble or DC offset -> high-pass filter"],
         headers=["audio/hpf.h", "audio/cascaded_biquad_filter.h"],
         unit=["Hpf", "CascadedBiquadFilter"], e2e=["E2E_HighPassFilter"],
         bench=[],
         scenario="A capture with 0.1 DC offset and 50 Hz hum through the high-pass filter; the DC and hum left "
                  "at the output, and the clean talker's level change, are measured.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The capture chain runs libwebrtc's filter (`audio/hpf.h`); the library's 4th-order Butterworth (v1) was retired in #565.", verification="On the v1 filter (retired in #565): no defects found; its response matched 4th-order Butterworth. #239: "
                      "`hpf::HighPassFilter` is libwebrtc M157's filter with its coefficient tables; it passes the "
                      "ports of `cascaded_biquad_filter_unittest.cc` (`CascadedBiquadFilter.*`), its measured "
                      "response matches the analytic response of libwebrtc's 16 kHz table within 0.05 dB "
                      "(`Hpf.FrequencyResponse16kHz`), and on `sm_apm_compare`'s case it gives hum attenuation "
                      "67.2 dB and speech -0.2 dB, as libwebrtc (`E2E_HighPassFilter.LibwebrtcHpfAsLibwebrtc`). "
                      "Its pass band is about 0.4 dB down, as libwebrtc's."),
    dict(id="6.6", slug="transient-suppressor", title="Transient (keyboard click) suppression",
         problems=["Keyboard typing noise -> transient suppressor"],
         headers=["audio/transient_suppressor.h"], unit=["TransientSuppressor"], e2e=["E2E_TransientSuppressor"],
         bench=["audio/TransientSuppressor_10ms"],
         scenario="Speech with a keyboard click every 250 ms; the click energy that falls in speech pauses is "
                  "compared with and without suppression.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. In the e2e chain it reads libwebrtc's pitch-based `VoiceActivityDetector` (probability above 0.5) to avoid attacking vowels (issue #671; before, the retired v1 `Vad::voiced()`). "
               "Keeping it as an opt-in (`enabled = true`) for later stages is an Owner decision (#243, 2026-10-22).",
         verification="Three defects were found and fixed. (1) The envelope started at 1e-8 and was frozen during "
                      "transients, so it locked into 'transient' forever. (2) The gain ramped down across the "
                      "first millisecond, which holds most of a click's energy; attack is now instant. (3) A "
                      "60 ms envelope still held the speech level right after a word, so a 4 ms envelope (onset "
                      "detection) is used. Attenuation in pauses went from 3 dB to 13.5 dB. #243: off by default "
                      "as libwebrtc (`TransientSuppressor.OffByDefaultAsLibwebrtc`: the audio passes unchanged); "
                      "the tests that measure it opt in with `enabled = true`."),
    dict(id="6.8", slug="vad", title="Voice activity detection",
         problems=["Silence wasting bandwidth -> VAD", "Detecting who's talking -> voice activity"],
         headers=["audio/vad.h"], unit=["Vad"], e2e=["E2E_Vad"],
         bench=["audio/Vad_10ms"],
         scenario="Speech in noise at about 10 dB SNR after noise suppression; recall and false-positive rate "
                  "(beyond a 250 ms hangover).",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. "
               "`Vad` is libwebrtc M157's `webrtc::Vad` (common_audio/vad/vad.cc, `CreateVad`) over the bit-exact "
               "`WebRtcVad` port in `agc1_vad.h` (issue #671); aggressiveness kVadNormal by default, as `sm_apm_compare` "
               "runs libwebrtc's. M157's `AudioProcessingImpl` reports no VAD decision, so the capture chain's per-frame "
               "VAD output is this `Vad`; the opt-in transient suppressor reads libwebrtc's pitch-based "
               "`VoiceActivityDetector` (the probability AGC1 reports), so a click is not taken for speech. 48 kHz is not "
               "ported (kError). ",
         verification="Issue #671 retired the library's own energy/spectral-flatness detector (v1) for libwebrtc's: "
                      "`Vad.ApiAsLibwebrtc` (vad_unittest.cc's rates, frame lengths and modes on the wrapper), "
                      "`Vad.IsWebRtcVadAndResets` (decisions equal `WebRtcVad`'s in every mode, and Reset() restarts "
                      "it as `VadImpl::Reset`), `Vad.NoiseLevelStepAsLibwebrtc` (a +20 dB step to -26 dBFS white "
                      "noise reads as speech in kVadNormal because the GMM caps its noise means, vad_core.c "
                      "`kMaximumNoise`; v1 learned the new floor). E2E_Vad recall 99.7 %, false positives 0 % "
                      "(libwebrtc kVadNormal in `sm_apm_compare`: 99.9 % and 0.0 % on the same talker behind the "
                      "high-pass filter). The v1 detector's history: musical noise after NS and a slow noise-floor "
                      "rise were fixed before #671."),
    dict(id="6.9", slug="audio-level", title="Audio level (RFC 6464)",
         problems=["Showing who is talking, and voice activity without decoding -> audio level header extension (RFC 6464)"],
         headers=["audio/audio_level.h", "core/rms_level.h"], unit=["AudioLevel", "RmsLevel"], e2e=["E2E_AudioLevel"],
         bench=["audio/AudioLevel_20ms"],
         scenario="Level and voice flag are encoded into the RFC 6464 byte and decoded again; the level of known "
                  "tones and silence must match.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. AudioLevelMeter is libwebrtc's voe::AudioLevel (issue #101): the stats level of a send stream, the maximum absolute sample refreshed every 11th frame and decayed to a quarter, and total energy and duration. The RFC 6464 header level is libwebrtc's RmsLevel (modules/audio_processing/rms_level.cc, issue #567): the mean square over every 10 ms chunk since the last Average(), as -dBov rounded as libwebrtc, 127 for digital silence and 126 for faint non-zero input, restarted when the chunk size changes. As ChannelSend, the send path computes it (issue #567): Session::AnalyzeAudioCapture feeds each 10 ms capture frame to the stream's RmsLevel in RtpSender (AnalyzeMuted for a muted frame after a muted one), and every audio packet carries Average() while the extension is mapped, unless the caller passes a level (the override); the voice bit is off for an Opus DTX frame. Session::ProcessAudioCapture also mutes the frame first as libwebrtc's AudioFrameOperations::Mute (issue #879, sm/core/audio_frame_operations.h): a 128-sample fade-out on the frame where mute starts, zeros while muted, a 128-sample fade-in on the frame where it ends. RmsLevel lives in core (sm/core/rms_level.h) so the RTP section can use it. ComputeLevel is RmsLevel on one frame. ", verification="Active speaker detection was removed with the other SFU-only components (issue #27); "
                      "the RFC 6464 codec and level are unchanged. RmsLevel (issue #567) is checked against libwebrtc's own RmsLevel "
                      "on 400 packets and the extension byte against libwebrtc's AudioLevelExtension in both directions "
                      "(sm_rtcp_interop)."),
    dict(id="6.11", slug="resampler", title="Sample-rate conversion",
         problems=["Device sample-rate mismatch -> resampler"],
         headers=["audio/spl_resampler.h", "audio/push_resampler.h"],
         unit=["SplResampler", "PushResampler"], e2e=["E2E_Resampler"],
         bench=[],
         scenario="44.1 kHz microphone -> 16 kHz processing (HPF) -> 48 kHz speaker with a 1 kHz tone, through "
                  "`PushResampler<float>`; "
                  "end-to-end SNR after fitting gain and phase, plus the output length.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. The Audio Device Module (platform audio I/O, device switching) is platform glue rather than an "
               "algorithm and is out of scope; the resampler is what bridges device and processing rates. "
               "libwebrtc's resamplers are ported (issue #565): `SplResampler` is `common_audio/resampler/resampler.cc`, "
               "the fixed-point int16 resampler on the signal processing library's allpass half-band and fractional "
               "filters (`resample_by_2.c`, `resample_by_2_internal.c`, `resample.c`, `resample_48khz.c`, "
               "`resample_fractional.c`), with its 21 modes, block sizes and coefficients; `PushResampler<T>` is "
               "`push_resampler.cc`, one `PushSincResampler` per channel (int16 or FloatS16, up to 8 channels), which "
               "`PushSincResampler` now also takes as int16, as libwebrtc. Differences: scratch buffers are members "
               "rather than malloc'ed per call, and the fixed-point sums wrap explicitly where libwebrtc relies on "
               "overflow under RTC_NO_SANITIZE (same output); the ARM/MIPS assembly variants are not ported. The "
               "library's v1 Kaiser `Resampler` was retired in #565.",
         verification="#565: `SplResampler` is bit-exact with libwebrtc's own `resampler.cc` and `resample*.c` "
                      "compiled from `reference/`, on all 94 rate pairs and channel counts that take 10 ms blocks "
                      "(8, 11, 16, 22, 32, 44, 48 and 96 kHz, mono and stereo; golden hashes in "
                      "`SplResampler.BitExactWithLibwebrtc`), and passes the ports of `resampler_unittest.cc` and "
                      "`push_resampler_unittest.cc`. Earlier, on the v1 resampler: defect found: when decimating, the filter used too few taps (transition band several kHz "
                      "wide), so a 10 kHz tone aliased at only -36 dB into 48 -> 16 kHz output. Taps now scale "
                      "with the decimation factor: 91 dB rejection, 107 dB SNR."),
    dict(id="6.12", slug="band-splitting", title="Band splitting",
         problems=["Wideband capture (32/48 kHz) through processing designed for 16 kHz -> APM band splitting (QMF, three-band filter bank)"],
         headers=["audio/splitting_filter.h"], unit=["SplittingFilter"], e2e=[],
         bench=["audio/SplittingFilter_48k_10ms"],
         scenario="Unit tests only: tones placed in each band at 32 and 48 kHz must land in their own band and come back "
                  "through synthesis; an impulse through the two-band QMF must match a direct evaluation of the all-pass "
                  "cascade.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#6-audio-quality) for the differences from libwebrtc. "
               "A port of libwebrtc's `SplittingFilter` (issue #241): `modules/audio_processing/splitting_filter.cc`, "
               "`three_band_filter_bank.cc` and `common_audio/signal_processing/splitting_filter.c`, with their "
               "coefficients. It is the first step of the `apm v2` ports (#240 AEC3, #241 NS), which run on the 0-8 kHz band. "
               "Differences: mono only (libwebrtc splits every channel of an `AudioBuffer`), and samples on this "
               "library's [-1, 1) scale, so the S16 saturation in the QMF synthesis is at [-1, 32767/32768].",
         verification="New in #241's first PR. The two-band analysis matches a direct-form evaluation of "
                      "`WebRtcSpl_AllPassQMF` to 1e-6 across two frames, and both paths were bit-exact with libwebrtc's own `splitting_filter.c` and `three_band_filter_bank.cc` compiled from `reference/` (the three-band files arrive there with #247; golden values pinned in `SplittingFilter.MatchesLibwebrtcOutput`); level through analysis and synthesis is "
                      "within 0.002 dB at 32 and 48 kHz on a 0.2-4.2 kHz chirp."),
    # ------------------------------------------------------------------ 7
    dict(id="7.1", slug="freeze-detector", title="Freeze detection",
         problems=["Lost frame (decode impossible) -> freeze the last frame until recovery (measured as freezes)"],
         headers=["video/freeze_detector.h"], unit=["FreezeDetector"], e2e=["E2E_FreezeDetector"],
         bench=["video/FreezeDetector_frame"],
         scenario="60 s of 30 fps video with 1% frame loss, two 600 ms outages and PLI-driven recovery; the "
                  "detector's freeze count is compared with gaps counted directly from the render log.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. Keyframe requests that end freezes are in 3.5.",
         verification="A unit-test expectation was wrong about the harmonic frame rate: irregular 38/38/23 ms "
                      "intervals give 28.97 fps, not 30.3, which is the point of the metric. "
                      "#268 ported libwebrtc's freeze rule (`video/video_quality_observer2.cc`, `OnRenderedFrame`): the "
                      "current interval enters the 30-interval average before the test, freezes stay in it, the "
                      "average is rounded down to whole ms and the test starts at 5 intervals. `sm_video_compare` now "
                      "gives libwebrtc's counts on every freeze input (9 vs 9 freezes at 180-230 ms gaps, 7 vs 7 at "
                      "3 fps, 57 vs 57 on random gaps), and `FreezeDetector.*` pins them "
                      "([fix record](../../fixes/268-freeze-rule-as-libwebrtc.md))."),
    dict(id="7.2", slug="codec-fallback", title="Hardware -> software codec fallback",
         problems=["Hardware encoder failure -> automatic fallback to a software encoder",
                   "Hardware decoder failure -> automatic fallback to a software decoder"],
         headers=["video/codec_fallback.h"], unit=["CodecFallback", "DecoderFallback"], e2e=["E2E_CodecFallback"],
         bench=["video/CodecFallback_frame"],
         scenario="A hardware encoder that starts failing at 10 s and asks for the software fallback on its "
                  "10th error; checks that plain errors don't switch, that exactly one fallback happens, that "
                  "the fallback is primed with the last rate and starts with a keyframe, and that the frame "
                  "that asked is encoded by the fallback, so the longest gap between encoded frames stays small.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. "
               "Since #573 a port of libwebrtc's VideoEncoderSoftwareFallbackWrapper, and since #607 of its "
               "VideoDecoderSoftwareFallbackWrapper (DecoderFallbackController); the header comment above lists what "
               "is not ported.",
         verification="#573 replaced the library's own CodecFallbackController (3 consecutive errors, 10% error "
                      "rate over 100 frames, hardware retry after 60 s with back-off, none of which libwebrtc has) "
                      "with the port. libwebrtc's unit test file is not in reference/; CodecFallback.* covers the "
                      "wrapper's cases from the source: init failure, both failing, simulcast parameters, "
                      "fallback request only from the main encoder, failed fallback init, release and re-init, "
                      "forced fallback by resolution and by the VP8 trial with its parameter checks, and the info merge. "
                      "#607 ported the decoder side (api/video_codecs/video_decoder_software_fallback_wrapper.cc): "
                      "fallback when the hardware decoder fails to configure, on FALLBACK_SOFTWARE from Decode (the "
                      "same frame decoded again by the fallback), or on the 4th consecutive keyframe error "
                      "(kMaxConsequtiveHwErrors; delta-frame errors don't count), sticky until Release, the forced "
                      "fallback trial WebRTC-Video-ForcedSwDecoderFallback (off), the 'A (fallback from: B)' name and "
                      "the hardware-decoded-frames histogram sample. Its unit test file is not in reference/; "
                      "DecoderFallback.* covers each path of the source."),
    dict(id="7.3", slug="orientation", title="Video orientation (CVO)",
         problems=["Rotated camera -> video orientation header extension (CVO)"],
         headers=["video/orientation.h"], unit=["Orientation"], e2e=["E2E_Orientation"],
         bench=["video/Orientation_rotate90_720p"],
         scenario="A phone rotating through all 4 rotations, with and without mirroring; frames carry CVO in a "
                  "real RTP header extension and the receiver must show the scene upright, bit-exact.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. "
               "On the wire (issue #567), rtp::RtpSender writes the extension as libwebrtc's RTPSenderVideo::AddRtpHeaderExtensions "
               "(rtp_sender_video.cc:388-408) when it is mapped: on the last packet of a keyframe, of a frame whose rotation changed, "
               "and of every frame whose rotation is not 0, with the byte of ConvertVideoRotationToCVOByte (rtp_cvo.h; the camera and "
               "flip bits 0). The packetizer leaves the room as last_packet_reduction_len. rtp::RtpReceiver gives each frame the "
               "rotation of its last packet, 0 without the extension (rtp_video_stream_receiver2.cc:797-802, 1108). "
               "Session::SendVideo takes it as EncodedVideoFrame::rotation. ",
         verification="No defects found. The CVO byte RtpSender writes is checked against libwebrtc's VideoOrientation "
                      "extension in both directions (sm_rtcp_interop, issue #567)."),
    dict(id="7.4", slug="content-policy", title="Content hints and screen-content variable frame rate",
         problems=["Screen sharing (text must stay sharp) -> content hint, screen-content coding tools, variable framerate",
                   "Talking head vs. motion -> content-type-aware rate control"],
         headers=["video/content_policy.h", "video/frame_cadence_adapter.h", "video/quality_convergence.h"],
         unit=["ContentPolicy", "FrameCadenceAdapter", "QualityConvergence"], e2e=["E2E_ContentPolicy"],
         bench=["video/FrameCadenceAdapter_frame", "video/Q_ScreenShare_encode_ratio"],
         scenario="60 s of screen sharing at max 15 fps (VP8, a QP model fed to QualityConvergenceController): a "
                  "new slide every 10 s and a blinking cursor, delivered by a zero-hertz capturer only when the "
                  "screen changes. Every change is encoded one frame period after it arrives; a static screen is "
                  "repeated at the frame period until the QP converges, then once per second.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. "
               "Screen-content coding tools (AV1 palette/intra-BC, HEVC SCC) are encoder features; the policy does not set them.",
         verification="No defects found. Issue #581: with no hint, camera video maps to maintain-framerate (the "
                      "`ContentSettings` default too), as libwebrtc's `GetDegradationPreference` "
                      "(`media/engine/webrtc_video_engine.cc:2032-2064`; BALANCED only with the "
                      "`WebRTC-Video-BalancedDegradation` trial, off); it was balanced. Issue #877 (Owner decision): "
                      "`SettingsFor` and `ContentSettings` carry only the degradation preference; the QP ceilings "
                      "(40 detail, 32 text), frame-rate floors (15, 2, 1), the screencast frame-rate cap and the "
                      "variable-frame-rate and screen-tools flags are removed, as libwebrtc derives none of them "
                      "from the hint. Nothing in the call read them, so no test bench metric moves. "
                      "Issue #878: the block-hash `FrameChangeDetector` (which skipped unchanged frames and sent a "
                      "1/s keep-alive) is replaced by a port of libwebrtc's `FrameCadenceAdapter` "
                      "(`video/frame_cadence_adapter.cc`: passthrough and `ZeroHertzAdapterMode`) and "
                      "`QualityConvergenceController`/`Monitor` (`video/quality_convergence_*.cc`), with their "
                      "constants: one frame period of delay, short repeats until quality converges, idle repeats "
                      "every 1000 ms, refresh-frame requests after 3 periods. Not ported: `queue_overload` (no "
                      "encoder queue here) and the VSync metronome mode (only with an encode metronome). "
                      "Deviation (issue #887, [fix record](../../fixes/887-zero-period-clamp.md)): the frame period "
                      "is at least 1 us. From a max fps of about 2e6, libwebrtc's 1 s / max fps rounds to 0 us and "
                      "each repeat is due when it is posted, which spins its encoder queue; here one `Process()` "
                      "call never returned. Rates below 2e6 fps, which every source reports, are unchanged."),
    dict(id="7.7", slug="frame-scheduler", title="Render timing and late-frame dropping",
         problems=["Frames arriving too late to render -> render timing and late-frame dropping"],
         headers=["video/frame_scheduler.h", "timing/frame_release.h", "video/render_frames.h"],
         unit=["FrameScheduler", "VcmTiming", "RenderFrames"],
         e2e=["E2E_FrameScheduler"],
         bench=["video/FrameScheduler_frame"],
         scenario="30 fps over 15 ms jitter: VideoJitterEstimator + VideoTiming give render times and the "
                  "scheduler presents frames. Render-interval jitter is compared with rendering on arrival.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. ", verification="Exposed the 4.6 jitter-offset defect (render-interval jitter 7.9 ms before the "
                                "fix, 1.1 ms after). "
                                "Timing v2 ([fix record](../../fixes/24-timing-v2.md), partial for #24): "
                                "`timing::FrameReleaseController` releases frames as libwebrtc's "
                                "VideoStreamBufferController and FrameDecodeTiming do. A late frame is skipped only "
                                "when a later temporal unit (here a keyframe) is decodable, so the 10 ms late-drop "
                                "rule is gone: with every v2 switch on, `issue_53_cwnd_pushback`'s link renders "
                                "28.8-29.2 fps on seeds 1-10 (timing v1 16.0-20.8, libwebrtc 27-28); "
                                "`issue_24_timing_v2_render_fps` locks it in. "
                                "Issue #437 (partial): `video::RenderFrames` ports libwebrtc's pre-render queue "
                                "(video/render/incoming_video_stream.cc, video_render_frames.cc), behind "
                                "`render_smoothing` (on by default since #437's flip, as libwebrtc). With it on, `very_low_capacity` seeds 1-10 render "
                                "466 -> 421 frames and freeze 25.3 -> 24.6 times (libwebrtc 369 and 21.3); "
                                "`issue_437_render_smoothing_very_low_capacity` locks it in "
                                "([fix record](../../fixes/437-render-smoothing.md)). "
                                "Issue #583: with timing v2 the receiver's frame buffer holds at most 800 frames as "
                                "libwebrtc's FrameBuffer (video_stream_buffer_controller.cc:46, frame_buffer.cc:84-106): "
                                "in a full buffer a delta frame is refused and a keyframe clears it, a frame at or "
                                "below the last decoded id is refused unless it is a keyframe with a newer RTP "
                                "timestamp, which clears it; unit tests `RtpReceiver.AFullFrameBuffer...` and "
                                "`RtpReceiver.AfterAPacketBufferReset...`."),
    dict(id="7.8", slug="quality-scaler", title="QP-based quality scaler",
         problems=["Region-of-interest quality -> quality scaler and per-codec QP control"],
         headers=["video/quality_scaler.h", "video/video_stream_adapter.h"],
         unit=["QualityScaler", "VideoStreamAdapter", "VideoAdapter"], e2e=["E2E_QualityScaler"],
         bench=["video/QualityScaler_frame"],
         scenario="An H.264-like encoder model (QP +6 per halving of bits per pixel) with bandwidth 2500 -> 300 "
                  "-> 2500 kbps; resolution follows the QP signals.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#7-video-quality-and-processing) for the differences from libwebrtc. Per-region QP (ROI) is an encoder-side feature built on top of these thresholds.",
         verification="An expectation was wrong: at 300 kbps, 640x360 already gives QP 33, which is inside the "
                      "H.264 band, so not going lower is correct. The test now checks that QP returns inside the "
                      "band. The test bench and `sm_compare` share an encoder model for this module's inputs "
                      "(`testbench/encoder_model.h`, issue #84 step 1): QP 30 at libwebrtc's per-resolution start "
                      "rates (720p 1000 kbps, 360p 500 kbps), +6 per halving of the bits per frame, clamped to 0..51, "
                      "and 10 ms per 720p frame of encode time. Deviation: a model in place of a codec, identical on "
                      "both stacks, so scaling decisions compare like for like. "
                      "`version = 2` (issue #84 step 2; the default since #579) is libwebrtc M157's `QualityScaler` "
                      "(`modules/video_coding/utility/quality_scaler.cc`) with `QualityScalingExperiment`'s default "
                      "smoothers: 2 s checks until the first adapt-down, then 5 s; 60 frames over a 150-frame "
                      "window; drop rate >= 60%; smoothed QP > high or <= low. `sm_video_compare` gives the same "
                      "signals as libwebrtc on all six quality-scaler inputs "
                      "([results](../../results/video-webrtc.md)); the unit tests port "
                      "`quality_scaler_unittest.cc`. "
                      "Issue #84 step 4 wires version 2 into the send path behind `quality_scaling` "
                      "(`rtp::VideoSendController`, scenario key `quality_scaling on`, off by default): "
                      "libwebrtc's `QualityScalerResource` with `MAINTAIN_FRAMERATE`, `VideoStreamAdapter`'s "
                      "resolution steps (3/5 down, 5/3 up with a 12/5 maximum, the 320x180 minimum, waiting for the "
                      "frame size to change), `VideoAdapter`'s 3/4-2/3 scale series and "
                      "`VideoStreamEncoder::DropDueToSize` (up to 4 initial frames dropped as too large for the "
                      "target, each adapting down), from `call/adaptation/` and `api/video/video_adapter.cc` "
                      "(reference files of #302). `sm_compare` runs libwebrtc with the same settings "
                      "(`ScalingSettings(24, 37)`, `MAINTAIN_FRAMERATE`, its own `VideoAdapter` in the source, and "
                      "`QualityScalingExperiment`'s default string, whose H.264 slot is 24/37; the model encoder reports "
                      "H.264 since #339). A CheckQpTask due at a frame's capture time runs after that frame was encoded, "
                      "and its restriction reaches the source through a worker-queue task "
                      "(`VideoStreamEncoder::OnVideoSourceRestrictionsUpdated`), so a step applies from the next "
                      "frame; verified on yardstick #364, where both stacks then give the same checks, steps and "
                      "freezes on `issue_23_outage_500ms` with scaling on "
                      "([fix #348](https://github.com/aray4702/streaming_media/issues/348)). Step 5 adds the CPU resource and "
                      "`ResourceAdaptationProcessor`'s per-resource bookkeeping "
                      "(`call/adaptation/resource_adaptation_processor.cc`): each resource keeps the restrictions it "
                      "last asked for, and an adapt-up applies only for the resource with the most adaptations, "
                      "alone (`kNotMostLimitedResource`, `kSharedMostLimitedResource`); it is `video::ResourceAdaptationProcessor`, "
                      "ported with the libwebrtc unit tests (#646). #646 also ports the removal of a resource, as "
                      "`VideoStreamEncoderResourceManager` does on an encoder info change "
                      "(`VideoSendController::OnEncoderInfoChanged`; `ConfigureQualityScaler`, "
                      "`ConfigureEncodeUsageResource`): the quality scaler stops without trusted QP thresholds (and the "
                      "initial frame drop with it, restarted when it is added again), the CPU check without "
                      "`enable_cpu_overuse_detection` (a started one restarts); `RemoveLimitationsImposedByResource` "
                      "clears the restrictions when the removed resource was the only adapted one, goes back to the "
                      "most limited remaining resource's restrictions and counters when it was more limited "
                      "(`VideoStreamAdapter::GetAdaptationTo`), and the removed resource's later signals are ignored. "
                      "The test bench's encoder never changes its info, so no scenario moves. #646 ports `VideoStreamAdapter`'s other "
                      "degradation preferences: MAINTAIN_RESOLUTION's frame-rate steps (2/3 down, 3/2 up, at least "
                      "2 fps), BALANCED (frame rate to `BalancedDegradationSettings`' min fps for the frame size "
                      "first, then resolution; up, the next config's fps first; `AdaptIfFpsDiffInsufficient`; "
                      "`BalancedConstraint`'s kbps limits; `GetAdaptDownResolution`), MAINTAIN_FRAMERATE_AND_RESOLUTION "
                      "(disabled), the clear on a switch to or from BALANCED and the restrictions filtered by the "
                      "preference, with `balanced_degradation_settings.cc`'s default configs (320x240: 7 fps, "
                      "480x360: 10 fps, 640x480: 15 fps), ported with the libwebrtc unit tests; the camera call stays "
                      "MAINTAIN_FRAMERATE (#635), so no scenario moves. #646 also ports the initial-drop reset on a start-bitrate drop "
                      "(`InitialFrameDropper::SetTargetBitrate`, behind QualityScalerSettings' "
                      "`initial_bitrate_interval_ms` / `initial_bitrate_factor`, unset by default as in M157) and "
                      "`pending_frame_`: a frame dropped for its size or a pause is held, the next frame releases "
                      "it (kEncoderQueue), and a resume encodes it at once if it fits and is under 1 s old "
                      "(`OnBitrateUpdated`, `kPendingFrameTimeoutMs`); unit tests cover each rule, and no scenario "
                      "pauses the encoder on either stack (the parity traces show only the 4 initial drops). The drop checks run "
                      "in `VideoStreamEncoder`'s order when quality scaling is on: the congestion-window drop "
                      "(`OnFrame`), then `DropDueToSize` and `OnMaybeEncodeFrame`, the outage pause "
                      "(`EncoderPaused`) and the frame dropper. `issue_84_quality_scaling_720p_low_rate` and "
                      "`issue_84_quality_scaling_rate_rise` lock in the behaviour."),
    # ------------------------------------------------------------------ 8
    dict(id="8.1", slug="rtp-to-ntp", title="RTP -> NTP mapping",
         problems=["Lip-sync (A/V sync) -> RTCP Sender Reports map the RTP timestamp to NTP wallclock"],
         headers=["sync/rtp_to_ntp.h"], unit=["RtpToNtp"], e2e=["E2E_RtpToNtp"],
         bench=["sync/RtpToNtp_UpdateAndEstimate", "sync/RtpToNtp_Estimate"],
         scenario="120 s with a sender audio clock 80 ppm off its NTP clock; SRs every second over a jittery "
                  "link. Every 20 ms the estimated capture time of the current RTP timestamp is compared with "
                  "the truth.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. ", verification="No algorithm defects; one unit test's reject counting was corrected. "
                      "#77 ported libwebrtc M157's SR validation (`rtc_base/rtp_to_ntp_estimator.cc`): it compares a new SR with the "
                      "newest one (NTP forward by at most 1 h, RTP forward by at most 1 << 25 ticks) and clears the history after 3 "
                      "invalid SRs, where ours rejected points 30 ms off the fitted line. The line is fitted in NTP units, as libwebrtc fits it. "
                      "The e2e capture-time error on the 80 ppm clock is unchanged (0.014 ms)."),
    dict(id="8.2", slug="stream-sync", title="Audio/video synchronization and CNAME sync groups",
         problems=["Lip-sync (A/V sync) -> the receiver aligns playout delay between streams",
                   "Synchronizing multiple streams from one sender -> shared CNAME, MediaStream grouping"],
         headers=["sync/stream_sync.h"], unit=["StreamSync"], e2e=["E2E_StreamSync"],
         bench=["sync/StreamSync_Update"],
         scenario="Audio (30 ms network, 60 ms buffer) and video (90 ms network, 40 ms decode/render) from one "
                  "CNAME, each playing out after max(its buffer, the minimum playout delay the synchronizer sets); "
                  "at 40 s the video path gains 120 ms. Measures the true A/V offset before sync, after settling and "
                  "after the route change.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. The 30 ms dead band sits inside the ITU-R BT.1359 detectability window (+45/-125 ms).",
         verification="The test's 'before' sample was taken after the first corrections; it is now taken at t=0 "
                      "(40 ms offset). #77 ported libwebrtc M157's `StreamSynchronization` and `RtpStreamsSynchronizer` "
                      "(`video/stream_synchronization.cc`, `video/rtp_streams_synchronizer2.cc`, `call/syncable.h`). The filter "
                      "starts at 0 and resets after each move, one stream changes at a time, the outputs are minimum playout delays "
                      "set through `Syncable`, and the synchronizer runs every 1 s when both streams have new packets. "
                      "The e2e offset after settling went from 18.8 to 25 ms, and after the route change from 22.0 to 17 ms. Both "
                      "are inside the 30 ms dead band. #77 step 2 wires it into the session behind `SessionConfig::av_sync` (scenario "
                      "`av_sync on`, off by default): `rtp::Session` pairs its first audio and video receive streams as `Call::ConfigureSync` "
                      "does for one `sync_group`, the receivers give `Syncable::Info` (the last SR and media packet; video's current delay is "
                      "`VCMTiming::TargetVideoDelay`, audio's NetEq's `FilteredCurrentDelayMs`), and `SessionConfig::audio_rtcp` sends the "
                      "audio stream's own SR, as libwebrtc's audio RtpRtcp module, which the audio side needs. On `issue_77_av_sync_clean_link` "
                      "the A/V offset median goes from -53 to -16 ms (seed 1); `sm_compare` sets libwebrtc's `sync_group` for the same scenario. #77 step 3 makes `av_sync` the test bench default on both stacks (scenario `av_sync off` keeps the old behaviour); the receive streams' `Syncable` glue was checked against `audio/channel_receive.cc` and `audio/audio_receive_stream.cc` (parity inventory 8.2a). Packets restored from RTX or recovered by FEC no longer move the video stream's latest received packet, as `RtpVideoStreamReceiver2` skips `recovered()` packets; before, a late retransmission looked like a 300 ms video delay and sync over-delayed audio (`issue_127_all_v2_no_stall` audio jitter buffer p95 350 -> 230 ms, libwebrtc 235). #821 checked the pairing against `Call::ConfigureSync` (`call/call.cc`): the session's pairing is the same for its one `av_sync` group, and `MediaSyncGroups` now pairs by sync group as `ConfigureSync` does (first audio with the first video only, none for an empty group, re-paired on removal) instead of the lowest SSRC of each kind per CNAME; `StreamSync.GroupPairsTheFirstAudioWithTheFirstVideoOnly`, `StreamSync.EmptySyncGroupIsNeverPaired`, `StreamSync.RemovingOrMovingAStreamReconfiguresItsGroups` and `Session.AvSyncPairsTheFirstAudioAndVideoReceiveStreams` pin it."),
    dict(id="8.3", slug="capture-time", title="Absolute capture time and glass-to-glass latency",
         problems=["Capture-to-render latency measurement -> absolute capture time extension, timing extensions"],
         headers=["sync/capture_time.h"], unit=["CaptureTime"], e2e=["E2E_CaptureTime"],
         bench=["sync/AbsCaptureTime_SenderAndInterpolator"],
         scenario="A sender 2.5 s ahead of the receiver; an SFU rewrites RTP timestamps but forwards "
                  "abs-capture-time; the receiver learns the clock offset from SRs and measures capture-to-render "
                  "latency for every frame.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. ", verification="Portability finding: interpolation used long double, which is software-emulated "
                                "on WebAssembly and made the benchmark miss its budget there (347 ns vs 200). "
                                "It now uses exact integer arithmetic. #567 part 5 ports libwebrtc M157's "
                                "`AbsoluteCaptureTimeSender`, `AbsoluteCaptureTimeInterpolator`, `CaptureClockOffsetUpdater` "
                                "and `RemoteNtpTimeEstimator` as units (`modules/rtp_rtcp/source/`): the sender's rules "
                                "(more than 1 s, an error that rounds to more than 1 ms, a rate of 0, `force`), the "
                                "interpolator retiring its extension at the first packet it can't interpolate, and the "
                                "clock offset as the median of the last 7 SRs once 3 are in, where it was a minimum with a "
                                "5% leak. `LatencyEstimator` computes capture time as `WebRTC-UseAbsCapTimeForG2gMetric` "
                                "does. #567 part 6 (send half): `RtpSender` writes the extension as libwebrtc's senders, "
                                "once per video frame on its first packet (whole NTP milliseconds, offset 0, 90 kHz, with "
                                "`first_packet_reduction_len`) and per audio packet (full precision), from the frame's "
                                "`capture_time`; `sm_rtcp_interop` checks the bytes against libwebrtc both ways. "
                                "#567 part 6 (receive half): `RtpReceiver` interpolates every media packet "
                                "(`AbsoluteCaptureTimeInterpolator`, video at 90 kHz, audio at the stream's clock rate; "
                                "rtp_video_stream_receiver2.cc:780-795, channel_receive.cc:747-754) and adds the local capture "
                                "clock offset (`CaptureClockOffsetUpdater`); a video frame takes its first packet's, with "
                                "`ntp_time_ms` from `RemoteNtpTimeEstimator::Estimate` (the trial off). After each RTCP packet "
                                "the SR trains `RemoteNtpTimeEstimator` once there is an RTT, video only with an SR that arrived "
                                "with that packet (DeliverRtcp, ReceivedRTCPPacket). unit.RtpReceiver and unit.Session "
                                "(`ReceivedFramesCarryTheCaptureTimeOnTheLocalClock`) pin it. #661: the estimator waits for the "
                                "stream's own module RTT, `ModuleRtpRtcpImpl2::LastRtt` (rtp_rtcp_impl2.cc:482-491: the "
                                "report-block RTT on the local SSRC, else the 1 s periodic update's XR DLRR RTT, video only, "
                                "as the audio receive stream has no RRTR by default), not CallStats' or the session's RTT; "
                                "decoded audio carries the packet infos of the primary packets each `GetAudio` decoded "
                                "(NetEq's `last_decoded_packet_infos_`), with the local capture clock offset taken at playout "
                                "(`GetAudioFrameWithInfo`, channel_receive.cc:472-507); unit.RtpReceiver "
                                "(`LastRttIsTheReportBlockRttElseThePeriodicOne`, `DecodedAudioCarriesThePacketInfosOfItsPackets`) "
                                "and unit.Session (`ReceiveStreamsTrainTheirNtpEstimateFromTheirOwnModulesRtt`) pin it. "
                                "Deviation (recorded on #661): `capture_to_render_ms` is a library metric that libwebrtc "
                                "reports only with the non-default `WebRTC-UseAbsCapTimeForG2gMetric` trial; it changes no "
                                "decision and no test bench or `sm_compare` metric reads it."),
    dict(id="8.4", slug="header-extensions", title="Header extensions (RFC 8285) and BUNDLE demuxing",
         problems=["Identifying streams -> SSRC, MID and RID header extensions"],
         headers=["sync/header_extensions.h"], unit=["HeaderExtensions", "RtpDemuxer"], e2e=["E2E_HeaderExtensions"],
         bench=["sync/HeaderExtensions_WriteParse4", "sync/RtpDemuxer_by_ssrc"],
         scenario="One 5-tuple carrying audio, three simulcast video encodings and RTX (repaired RID) with "
                  "reordering; MID/RID are only on each SSRC's first 10 packets, and one stream changes SSRC "
                  "mid-call. Routing accuracy.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#8-synchronization) for the differences from libwebrtc. "
               "The extension map and elements follow `RtpHeaderExtensionMap` and `RtpPacket` (#567, PR #598), and `RtpDemuxer` "
               "follows `call/rtp_demuxer.cc` (#567): criteria with MID, RSID, SSRCs and payload types, libwebrtc's conflict rules, "
               "and `ResolveSink`'s order (match-any, drop an unknown MID, MID/RSID from the packet or latched per SSRC, signalled "
               "or bound SSRC, unique payload type), with at most 1000 SSRC bindings.",
         verification="#567: unit.RtpDemuxer pins each rule of `ResolveSink` and `CriteriaWouldConflict`; "
                      "`sm_rtcp_interop` (CI job compare-webrtc) runs this demuxer and libwebrtc's on the same random sinks "
                      "and packets and checks that every packet goes to the same sink."),
    # ------------------------------------------------------------------ 9
    dict(id="9.1", slug="rtcp-engine", title="RTCP engine, SDES and BYE",
         problems=["Receiver and sender reports, RTT and feedback -> one RTCP engine per session",
                   "Early feedback (NACK, PLI, FIR, loss notification, transport-cc, REMB) -> sent at once, one datagram per libwebrtc send call"],
         headers=["rtp/rtcp_engine.h", "rtp/rtcp_sdes_bye.h"], unit=["RtcpEngine", "RtcpSdesBye"], e2e=["E2E_RtpSession"],
         bench=[],
         scenario="Two sessions exchange bytes over sim::NetworkLink (tests/e2e/rtp_session_e2e_test.cc): reports "
                  "flow in both directions at the scheduled intervals, RTT is measured from SR and RR, and feedback "
                  "requests reach the module that acts on them.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Follows libwebrtc's RTCPSender and RTCPReceiver for the report interval (1 s with video, 5 s "
               "without, randomization 0.5 to 1.5x, first report at half the nominal interval with no random draw, #360) and for the compound layout "
               "(SR or RR first, SDES CNAME after it: every SR carries SDES, with an empty CNAME if none was set, and an RR carries it "
               "only when the module has a CNAME, RTCPSender::PrepareReport, rtcp_sender.cc:713; rtp::Session's RR stands for libwebrtc's "
               "receive-only module, which has no CNAME, so it carries no SDES, issue #820). A BYE goes out with the full report and last "
               "in its compound (rtcp_sender.cc:631-650); like libwebrtc M157, which never sets kRtcpBye, the session never sends one "
               "(issue #820). Reports are timed as (issue #76 group 3; the fixed-grid v1 and "
               "the `rtcp` switch were deleted in #167) "
               "RTCPSender::ComputeTimeUntilNextReport: whole milliseconds drawn from [0.5, 1.5] x the minimum with libwebrtc's `webrtc::Random` (xorshift64*, `XorshiftRandom`; rtc_base/random.cc) seeded as `RTCPSender::random_` is, with the clock in microseconds when the engine is built (the caller passes it in; the test bench passes sm_compare's clock start to every engine, so both stacks draw the same intervals, issue #517), at least "
               "1 ms, and min(360 s / kbit/s, interval) for a video sender above 72 kbit/s; the rate is the sender's egress rate over 1 s, every packet type with its RTP header, as `RtpSenderEgress::GetSendRates(now).Sum()` (issue #217). A report due within 100 ms is sent ahead of a video keyframe (RTCPSender::TimeToSendRTCPReport, issue #101). NACK, PLI, FIR and loss notification carry the full SR/RR (report blocks) and SDES and restart the report timer, as RTCPSender::PrepareReport does in compound mode; transport-cc and REMB keep an empty leading RR where libwebrtc's SendCombinedRtcpPacket sends none (issue #201). Each libwebrtc send call is its own datagram: transport-cc and REMB (each a SendCombinedRtcpPacket call) never share one with a NACK, PLI, FIR or loss notification compound (SendRTCP), and the session builds its transport feedback before its receivers' 20 ms NACK tick, the order libwebrtc's task queue runs them in when both are due at once, since ReceiveSideCongestionController's task was posted first (issue #860). With `xr_rrtr` (libwebrtc's `rtcp_xr.receiver_reference_time_report`) a receive-only engine adds an RFC 3611 RRTR to every report and takes the RTT from the sender's DLRR echo (RTCPSender::BuildExtendedReports, RTCPReceiver::HandleXrDlrrReportBlock); every engine echoes the RRTRs it received in a DLRR on its next report, and the XR goes last in the compound (issue #356). Differs in having one engine per session where libwebrtc has "
               "one per SSRC, which one-to-one calls allow. Wire layouts are checked against libwebrtc's parsers in "
               "compare/webrtc/rtcp_interop.cc (CI job compare-webrtc).",
         verification="Introduced by #32 plan PRs 1 and 2. The Owner checked the scheduling constants against "
                      "libwebrtc M157 (rtcp_sender.cc, video_send_stream.h, audio_send_stream.h), see the issue. "
                      "#517: unit.RtcpEngine checks that the interval draws are libwebrtc's from the same seed; on seed 1 "
                      "the sender's RTCP send times match libwebrtc's on audio_only_lossy (6 of 6) and the first 9 on high_jitter. "
                      "#484: `SessionConfig::audio_receive_rtcp` (on by default since #484 part 2, libwebrtc's behaviour) gives the audio receive streams "
                      "their own engine, as libwebrtc's ChannelReceive (audio/channel_receive.cc:115-139, 608): an RR with the audio "
                      "block alone every 5 s, no SDES, and the video compounds carry only the video and RTX blocks; on seed 1 the "
                      "sender's audio report blocks match libwebrtc's in number and time on high_jitter, random_loss_5pct and "
                      "clean_link (6 of 6; [fix record](../../fixes/484-audio-report-blocks.md)). "
                      "#820: the SDES CNAME, BYE and FIR sequence-number send rules compared with rtcp_sender.cc; SDES ported "
                      "(unit.RtcpEngine.SenderReportCarriesSdesEvenWithoutACname, ReceiverReportCarriesSdesOnlyWithACname, "
                      "ByeGoesLastInACompoundWithTheFullReport, unit.Session.SenderReportsCarryTheCnameAndReceiveOnlyReportsCarryNoSdes) "
                      "and checked both ways in sm_rtcp_interop. "
                      "#860: one datagram per send call and transport feedback ahead of the NACK tick "
                      "(unit.RtcpEngine.TransportFeedbackNeverSharesADatagramWithACompound, "
                      "unit.Session.TransportFeedbackLeavesInItsOwnDatagramAheadOfANackDueAtTheSameTime); on seed 1 of "
                      "issue_12_lossbwe_policer the forward link now matches libwebrtc's packet for packet through 690 ms "
                      "(104 rows), where it matched through 410 ms (60 rows) before. "
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
               "budget (SendSideController::ComputeMeasuredRates) takes off the share of the video stream's send rate "
               "that was RTX or FEC, capped at 50%, as libwebrtc's FecControllerDefault::UpdateFecRates "
               "(compare/webrtc/reference/modules/video_coding/fec_controller_default.cc). By default the share is "
               "of the last second's bytes when `send_rate_stats` is off. `send_rate_stats` (default on since #224/#204, with `control_handler`) measures it as libwebrtc does, from one "
               "RateStatistics per media type (RtpSenderEgress::GetSendRates, rtp_sender_egress.cc:340, "
               "rtc_base/rate_statistics.cc): a repair stream that starts after 1.5 s without one counts at its rate "
               "since it started, so the share reaches the cap within about 100 ms of the RTX that follows a capacity "
               "drop, instead of over a second. Each packet counts in those rates as the pacer sends it "
               "(RtpSenderEgress::SendPacket, UpdateRtpStats), before the sent-packet notification that posts a "
               "target runs after the pacer's loop, and an allocation reads them at the current time "
               "(GetSendRates(now)), so the share it takes counts every packet of the loop (issue #875). There is no "
               "retransmission rate cap, as in M157. Differences that remain: the FEC amount (#7, "
               "FecControllerDefault's media_opt_util choice) and keyframe requests (#23); the encoder floor of 30 kbps "
               "applies even when the audio alone exceeds the target (#37).",
         verification="Introduced by #32 plan PRs 3 and 5. Issue #21 (partial) added the measured repair share; "
                      "see docs/fixes/21-repair-within-target.md. Issue #224 added `send_rate_stats` (sm::RateStatistics, "
                      "unit tests RateStatistics.* and SendSideController.RateStatistics*), off until it is judged with "
                      "the allocation-time encoder target of #204's `control_handler`. Issue #875 counts each packet in the send rates in the pacer's loop and reads them at the current time "
                      "(unit test SendSideController.AllocationPostedBySentPacketCountsTheWholeSendLoop; issue_12_lossbwe_policer seed 1 "
                      "with #865 matches libwebrtc's forward link through 1.21 s instead of 0.69 s). Review notes from PR 3 (padding timestamp, padding size "
                      "accounting, zero-byte frames) were fixed in PR 5. Issue #577 ported RTPSender::"
                      "ExpectedPerPacketOverhead (RtpSender::ExpectedPerPacketOverhead; unit tests "
                      "RtpSender.ExpectedPerPacketOverhead* and Session.ExpectedPacketOverheadIsTheVideoSendersHeader) "
                      "behind `expected_packet_overhead`, on by default since the flip (#577 part 2). "
                      "Per-suite results are recorded by ctest "
                      "and CI; tools/run_all.sh does not run these suites yet."),
    dict(id="9.3", slug="rtp-receiver", title="RTP receiver",
         problems=["Parsing, demultiplexing and repair on the receive path -> RtpReceiver",
                   "Frame assembly, keyframe requests and playout scheduling -> RtpReceiver"],
         headers=["rtp/rtp_receiver.h"], unit=["RtpReceiver"], e2e=["E2E_RtpReceiver"], bench=[],
         scenario="Video and audio streams over lossy, reordering links: RTX, FEC and NACK recover packets, the "
                  "packet buffer releases decodable frames, and audio is unwrapped from RED into the jitter buffer.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Frame ids and dependencies come from sequence numbers rather than a descriptor (design 6.4). LNTF is "
               "sent only when negotiated and frame information is present, so it is off for H.264, as in "
               "libwebrtc (follow-up #47). `sps_pps_tracker` (on by default for H.264 streams, as libwebrtc; issue #453): libwebrtc's "
               "H264SpsPpsTracker rule that drops the first packet of an IDR slice received before its PPS and "
               "that PPS's SPS and requests a keyframe (RtpVideoStreamReceiver2::OnReceivedPayloadData, kRequestKeyframe). "
               "Parameter sets are kept by id, as CopyAndFixBitstream keeps them (#568), and an SPS or PPS without "
               "ids (an FU-A fragment) is dropped without a request (kDrop). With it, the "
               "packet buffer's `missing_packets` hold (4.1, `VideoStreamConfig::packet_buffer`) keeps the delta "
               "frames after a dropped IDR start from reaching the first-frame-delta keyframe request, as "
               "libwebrtc's PacketBuffer does. Both are on by default in a session's H.264 video streams and off for "
               "other codecs, as libwebrtc (Owner's decision A on #453); RtpReceiverConfig and PacketBufferConfig "
               "keep them off, because they do not know the codec.",
         verification="Introduced by #32 plan PRs 4 and 6. Per-suite results are recorded by ctest and CI; "
                      "tools/run_all.sh does not run these suites yet. Issue #453: with `sps_pps_tracker on`, "
                      "`high_jitter` seeds 1-10 time to first frame 228 -> 449 ms (libwebrtc 433) and keyframe "
                      "requests 0.4 -> 1.8 (libwebrtc 1.2); the same value as libwebrtc on 5 of 10 seeds. With "
                      "`h264_missing_packets on` as well, keyframe requests 1.2, the same as libwebrtc on 10 of 10 "
                      "seeds (and on every seed of the other high_jitter copies). With both on by default, `high_jitter` seeds 1-10 "
                      "time to first frame 228 -> 454 ms (libwebrtc 453) and keyframe requests 0.4 -> 1.2 (libwebrtc 1.2)."),
    dict(id="9.4", slug="session", title="RTP session facade",
         problems=["Wiring the building blocks by hand -> one rtp::Session per endpoint of a one-to-one call"],
         headers=["rtp/session.h", "rtp/call_stats.h"], unit=["Session", "CallStats"], e2e=["E2E_RtpSession"], bench=[],
         scenario="The test bench runs a full call through two rtp::Session objects, and every packet crosses "
                  "sim::NetworkLink as bytes. The whole scenario suite is the end-to-end test.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Pull-based: every output is a Take*() call, so the order of effects is fixed. The app keeps the "
               "codecs, the frame dropper and the quality scaler, and reads the rates the session publishes. "
               "The video receivers take their RTT from CallStats (video/call_stats2.cc: the maximum of the reports "
               "of the last 1.5 s, on each report and every 1 s), fed by the 1 s periodic update; a receive-only "
               "endpoint's measurement comes from the XR DLRR echo with `rtcp.xr_rrtr` (as libwebrtc's GetAndResetXrRrRtt, issue #356; the test "
               "bench turns it on with video, as sm_compare does on libwebrtc's side), else from SetRtt (#217).",
         verification="Introduced by #32 plan PRs 5 and 6. Switching the test bench changed baseline metrics only "
                      "through real packet sizes; each change is explained in PR #46. Per-suite results are "
                      "recorded by ctest and CI; tools/run_all.sh does not run these suites yet."),
    dict(id="9.5", slug="h264", title="H.264 packetization (RFC 6184) and fake encoder",
         problems=["Video payload format on the wire -> H.264 packetization-mode 1: single NAL, STAP-A, FU-A; mode 0: single NAL units"],
         headers=["rtp/h264.h", "rtp/fake_h264.h", "rtp/sps_vui_rewriter.h"], unit=["H264", "SpsVuiRewriter"], e2e=["E2E_RtpSession"], bench=[],
         scenario="The test bench's encoder model emits SPS, PPS and IDR on keyframes and one slice on delta "
                  "frames; the packetizer splits them, the depacketizer rebuilds the access unit.",
         notes="See the [libwebrtc parity inventory](../../design/libwebrtc-parity.md#9-rtp-session-layer) for the differences from libwebrtc. Follows libwebrtc's RtpPacketizerH264 and VideoRtpDepacketizerH264 (checked by the interop test "
               "against H26xPacketBuffer). The fake encoder follows FakeH264Encoder and differs in making a keyframe "
               "when asked, in valid SPS and PPS of the filler's sizes (sm_compare's model encoder, #339), and in "
               "never writing a zero byte into the filler. An IDR alone counts as a keyframe, as in "
               "libwebrtc by default. Before packetizing a keyframe the session rewrites its SPS VUI as "
               "libwebrtc's FrameEncodeMetadataWriter::UpdateBitstream and SpsVuiRewriter do (#342): a VUI with "
               "bitstream_restriction (no frame reordering) is added or fixed, and AUDs are dropped. There is no "
               "color space input (the encoders modelled set none, so the video signal type is copied). The "
               "depacketizer ports VideoRtpDepacketizerH264::Parse (#568): which payloads it drops (an SPS, PPS or "
               "slice header that doesn't parse, a nested STAP-A or FU-A, an FU-A under 2 bytes), where frames start, "
               "the SPS's resolution, and the incoming SPS VUI rewrite; reassembly follows "
               "H264SpsPpsTracker::CopyAndFixBitstream. The SPS fields are read as libwebrtc's SpsParser, with "
               "H264::IsValidResolution's level 5.2 bound, and the PPS and slice ids as PpsParser::ParsePpsIds and "
               "ParseSliceHeader (ids bounded by kMaxPpsId and kMaxSpsId). Each unit's ids go to the receiver's "
               "parameter-set tracker (RTPVideoHeaderH264::nalus). The packetization mode is the one the host "
               "negotiated (VideoStreamConfig::h264_packetization_mode, mode 1 by default): mode 0 sends each NAL "
               "unit alone and fails a frame with a unit that does not fit, as RtpPacketizerH264::PacketizeSingleNalu "
               "(#655). Session::SendVideo returns the size of the access unit after the rewrite, which "
               "VideoStreamEncoder::RunPostEncode gives the frame dropper and the bitrate adjuster.",
         verification="Introduced by #32 plan PR 7. The 24-byte SPS+PPS packet that now opens the call exposed a "
                      "start-up fragility in the pacer and probing code (first probe cluster rejected), tracked in "
                      "#48. Per-suite results are recorded by ctest and CI; tools/run_all.sh does not run these "
                      "suites yet. #342: the keyframe's STAP-A [SPS, PPS] is 77 B on both stacks (72 B before), "
                      "checked on `clean_link`'s per-packet trace against libwebrtc's, and the rewritten bytes are "
                      "checked against libwebrtc's SpsVuiRewriter by `sm_rtcp_interop` (25 access units). #568: the "
                      "depacketizer's accept and drop rules, frame starts and receive-side SPS rewrite are unit-tested "
                      "against video_rtp_depacketizer_h264.cc case by case (`unit.H264`); the bench's frames are "
                      "rewritten on send, so the receive side finds kVuiOk and no suite metric moves. The PPS and "
                      "slice id bounds, the SPS resolution bound and the per-unit ids are unit-tested against "
                      "pps_parser.cc, h264_common.h and video_rtp_depacketizer_h264.cc (`unit.H264`, "
                      "`unit.SpsVuiRewriter`); no suite metric moves."),
]

# Problem rows from sections 3-8 that are platform glue, codec internals or
# covered as part of another algorithm. Listed in the coverage table.
NOT_IMPLEMENTED = [
    ("3", "Opus in-band FEC (LBRR), Opus DRED", "Inside the Opus codec; used by configuring the codec. 3.4 is the codec-agnostic fallback."),
    ("3", "Keyframe pacing, intra refresh, LTR frames", "Encoder features (codec-specific bitstream tools)."),
    ("3", "Temporal scalability (L1T2/L1T3) and temporal layer filtering", "Removed in #572: single-layer H.264 in libwebrtc M157 has no temporal layers (the patterns belong to the VP8/VP9/AV1 encoders), and forwarding a subset of layers is an SFU's job, which a one-to-one call does not need (program.md 0.1; Builder's and Owner's decisions on #566)."),
    ("4", "Clock drift estimation and compensation", "Removed in #590: libwebrtc M157's one-to-one call has no such component; it absorbs drift with NetEq's time stretch (4.4) and the RTP-to-NTP estimator and timestamp extrapolator (4.2, 8). Nothing in `rtp::Session`, the test bench, `web/` or `compare/` used it (Owner's A on #566)."),
    ("6", "DTX and comfort noise (RFC 3389)", "Removed in #591: libwebrtc M157's one-to-one call has no such component on the Opus path (Opus DTX is off without `usedtx=1`, and nothing in `rtp::Session`, the test bench, `web/` or `compare/` used it); the Owner's A on #566."),
    ("6", "Audio Device Module (platform I/O, mute, device switching)", "Platform glue per OS; the algorithms take 10 ms PCM frames. Rate bridging is 6.11."),
    ("7", "Codec negotiation (SDP offer/answer, H.264 profile-level-id)", "Removed in #592: SDP stays with the host stack (program.md 0.1); libwebrtc M157's one-to-one H.264 call takes its codec from the negotiated SDP in `webrtc_video_engine.cc`, outside this library (the Owner's A on #566)."),
    ("7", "Resolution scaling and aspect handling (`Scale`, `ScaleToFit`, `Crop`)", "Removed in #593: libwebrtc M157's one-to-one call has no such component (it scales with libyuv's `I420Buffer::Scale`, and the frames are the app's), and nothing in `rtp::Session`, the test bench, `web/` or `compare/` used it; the Owner's A on #566."),
    ("7", "Error resilience inside the decoder", "Codec internal; 7.2 handles the fallback decision."),
    ("7", "Screen-content coding tools (palette, intra-BC)", "Encoder features; 7.4 flags when to enable them."),
]
