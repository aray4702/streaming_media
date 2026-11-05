# Verification results

Produced by `tools/run_all.sh all`; regenerate with `tools/gen_docs.py`.

## Platforms

| Platform | Compiler | Runtime | Date | CTest |
|---|---|---|---|---|
| macOS arm64 (native) | Apple clang version 21.0.0 (clang-2100.4.1.1) | native | 2026-11-05T22:44:43Z | 100% tests passed out of 631 |
| WebAssembly (Node) | emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) 6.0.10-git | node v26.10.0 | 2026-11-05T22:50:19Z | 100% tests passed out of 469 |
| ASan+UBSan (Debug) | Apple clang version 21.0.0 (clang-2100.4.1.1) | native | 2026-11-05T23:17:19Z | 100% tests passed out of 619 |
| -fno-exceptions -fno-rtti | Apple clang version 21.0.0 (clang-2100.4.1.1) | native | 2026-11-05T23:22:32Z | 100% tests passed out of 541 |

CTest has one entry per test suite (one unit and one end-to-end suite per algorithm) plus one benchmark gate per section (`bench.<section>`, run with a short minimum time).

- **macOS arm64 (native)**: 2987/2987 test cases passed, 139/139 benchmarks passed.
- **WebAssembly (Node)**: 2074/2074 test cases passed, 100/100 benchmarks passed.
- **ASan+UBSan (Debug)**: 2987/2987 test cases passed.
- **-fno-exceptions -fno-rtti**: 2436/2436 test cases passed.

## Benchmarks

| Benchmark | macOS arm64 (native) | WebAssembly (Node) |
|---|---|---|
| `core/Histogram_Add` | PASS 3.8 ns | PASS 8.0 ns |
| `core/RealFft_256` | PASS 629.0 ns | PASS 1.23 us |
| `core/RtpPacket_SerializeParse` | PASS 86.4 ns | PASS 360.8 ns |
| `loss/NackTracker_PacketWith5PctLoss` | PASS 145.7 ns | PASS 198.1 ns |
| `loss/RtxSender_StoreAndResend` | PASS 77.1 ns | PASS 102.6 ns |
| `loss/FecEncode_10pkt_frame_50pct` | PASS 148.9 ns | PASS 1.07 us |
| `loss/FecDecode_RecoverOne` | PASS 242.6 ns | PASS 465.1 ns |
| `loss/RedEncodeParse_d2` | PASS 479.6 ns | PASS 378.3 ns |
| `loss/KeyframeRequester_Frame` | PASS 95.3 ns | PASS 61.6 ns |
| `loss/FecControllerDefault_UpdateFecRates` | PASS 23.2 ns | PASS 29.3 ns |
| `loss/Q_NackRtx_5pct_rtt80` | PASS (quality) | PASS (quality) |
| `loss/Q_Fec_5pct_rtt300` | PASS (quality) | PASS (quality) |
| `loss/Q_Red_10pct` | PASS (quality) | PASS (quality) |
| `loss/Q_Keyframe_2pct_frame_loss` | PASS (quality) | PASS (quality) |
| `loss/Q_Protection_adaptive_rtt300` | PASS (quality) | PASS (quality) |
| `timing/PacketBuffer_InsertAssemble` | PASS 163.8 ns | PASS 145.9 ns |
| `timing/ReceiveStatistics_OnPacket` | PASS 3.8 ns | PASS 4.6 ns |
| `timing/DelayManager_Update` | PASS 147.3 ns | PASS 160.5 ns |
| `timing/TimeStretch_Accelerate30ms_16k` | PASS 2964x RT | PASS 2584x RT |
| `timing/AudioJitterBuffer_10msPull_16k` | PASS 28148x RT | PASS 7488x RT |
| `timing/VideoJitterEstimator_Update` | PASS 24.8 ns | PASS 37.7 ns |
| `timing/Q_AudioJitterBuffer_jitter25_loss2` | PASS (quality) | PASS (quality) |
| `timing/Q_TimeStretch_stall600ms` | PASS (quality) | PASS (quality) |
| `timing/Q_VideoJitter_late_frames` | PASS (quality) | PASS (quality) |
| `timing/Q_Plc_10pct_speech` | PASS (quality) | PASS (quality) |
| `cc/TransportFeedback_Build100_SerializeParse` | PASS 49.9 ns | PASS 52.3 ns |
| `cc/Gcc_OnFeedback_per_packet` | PASS 186.8 ns | PASS 214.3 ns |
| `cc/Pacer_EnqueueProcess` | PASS 95.1 ns | PASS 95.9 ns |
| `cc/BitrateAllocator_5streams` | PASS 623.6 ns | PASS 371.9 ns |
| `cc/FrameDropper_and_Overshoot` | PASS 305.7 ns | PASS 340.2 ns |
| `cc/CpuOveruseDetector_PerFrame` | PASS 33.1 ns | PASS 46.8 ns |
| `cc/Q_Gcc_capacity_steps` | PASS (quality) | PASS (quality) |
| `cc/Q_LossBwe_15pct` | PASS (quality) | PASS (quality) |
| `cc/Q_Pacer_audio_delay` | PASS (quality) | PASS (quality) |
| `cc/Q_Probing_rampup_5mbps` | PASS (quality) | PASS (quality) |
| `cc/Q_Allocator_low_bandwidth` | PASS (quality) | PASS (quality) |
| `cc/Q_FrameDropper_overshoot160` | PASS (quality) | PASS (quality) |
| `cc/Q_Outage_recovery` | PASS (quality) | PASS (quality) |
| `audio/Vad_10ms` | PASS 15598x RT | PASS 12628x RT |
| `audio/SplittingFilter_48k_10ms` | PASS 6482x RT | PASS 1879x RT |
| `audio/TransientSuppressor_10ms` | PASS 85901x RT | PASS 87050x RT |
| `audio/AudioLevel_20ms` | PASS 82309x RT | PASS 59563x RT |
| `audio/HighPassFilter_10ms` | PASS 8993x RT | PASS 6682x RT |
| `audio/NoiseSuppressor_10ms` | PASS 3985x RT | PASS 2112x RT |
| `audio/Agc1_10ms` | PASS 747x RT | PASS 627x RT |
| `audio/Agc2_10ms` | PASS 324x RT | PASS 238x RT |
| `audio/Resampler_48k_to_16k_10ms` | PASS 6835x RT | PASS 3104x RT |
| `audio/Resampler_44k1_to_48k_10ms` | PASS 2246x RT | PASS 1035x RT |
| `audio/SplResampler_48k_to_16k_10ms` | PASS 1537x RT | PASS 3447x RT |
| `audio/Mixer_6sources_48k_10ms` | PASS 7005x RT | PASS 4948x RT |
| `audio/Q_EchoCanceller_full_duplex_call` | PASS (quality) | PASS (quality) |
| `audio/Q_EchoDelay_route_change` | PASS (quality) | PASS (quality) |
| `audio/Q_NoiseSuppressor_7dB_snr` | PASS (quality) | PASS (quality) |
| `audio/Q_Agc_quiet_then_loud_talker` | PASS (quality) | PASS (quality) |
| `audio/Q_Mixer_5_loud_talkers` | PASS (quality) | PASS (quality) |
| `video/FreezeDetector_frame` | PASS 4.0 ns | PASS 5.5 ns |
| `video/CodecFallback_frame` | PASS 1.6 ns | PASS 1.2 ns |
| `video/Orientation_rotate90_720p` | PASS 51x RT | PASS 64x RT |
| `video/FrameCadenceAdapter_frame` | PASS 41.6 ns | PASS 47.2 ns |
| `video/FrameScheduler_frame` | PASS 25.0 ns | PASS 23.2 ns |
| `video/QualityScaler_frame` | PASS 5.8 ns | PASS 7.8 ns |
| `video/SimulcastRateAllocator_3layers_update` | PASS 77.2 ns | PASS 48.2 ns |
| `video/Q_ScreenShare_encode_ratio` | PASS (quality) | PASS (quality) |
| `sync/RtpToNtp_UpdateAndEstimate` | PASS 40.3 ns | PASS 38.8 ns |
| `sync/RtpToNtp_Estimate` | PASS 2.6 ns | PASS 2.2 ns |
| `sync/StreamSync_Update` | PASS 6.5 ns | PASS 6.1 ns |
| `sync/AbsCaptureTime_SenderAndInterpolator` | PASS 6.0 ns | PASS 6.8 ns |
| `sync/HeaderExtensions_WriteParse4` | PASS 1.01 us | PASS 703.3 ns |
| `sync/RtpDemuxer_by_ssrc` | PASS 86.7 ns | PASS 63.1 ns |
| `rtp/SdesCname_SerializeParse` | PASS 133.5 ns | PASS 110.7 ns |
| `rtp/SimulcastSend_3layers_frame` | PASS 184.1 ns | PASS 343.4 ns |
| `rtp/Bye_SerializeParse` | PASS 138.6 ns | PASS 94.9 ns |
| `rtp/RtcpEngine_CompoundReportBuildAndParse` | PASS 915.6 ns | PASS 756.2 ns |
| `rtp/RtcpEngine_NackPliFeedbackBuildAndParse` | PASS 1.32 us | PASS 969.7 ns |
| `rtp/RtpSender_SendVideoFramePaced` | PASS 34.86 us | PASS 16.71 us |
| `rtp/RtpReceiver_ReceivePacket` | PASS 1.04 us | PASS 1.06 us |
| `rtp/Session_VideoFrameEndToEnd` | PASS 62.31 us | PASS 68.91 us |
| `rtp/Q_Session_Video_5pct_loss` | PASS (quality) | PASS (quality) |
| `rtp/H264_Keyframe1080p_PacketizeDepacketize` | PASS 90.90 us | PASS 162.72 us |
| `rtp/H264_DeltaFrame1080p_PacketizeDepacketize` | PASS 12.83 us | PASS 22.12 us |
| `p2p/Stun_ParseIceRequest` | PASS 515.8 ns | PASS 450.6 ns |
| `p2p/Stun_BuildBindingResponse` | PASS 683.5 ns | PASS 517.7 ns |
| `p2p/UdpPort_GatherHostAndSrflx` | PASS 3.69 us | PASS 4.35 us |
| `p2p/Connection_CheckRoundTrip` | PASS 4.55 us | PASS 6.56 us |
| `p2p/PortAllocator_GatherSession` | PASS 6.81 us | PASS 8.13 us |
| `p2p/IceController_SortPingPrune` | PASS 391.6 ns | PASS 270.8 ns |
| `p2p/IceAgent_ConnectBehindNats` | PASS 68.89 us | PASS 72.13 us |
| `p2p/IceAgent_SendPacket` | PASS 347.8 ns | PASS 386.2 ns |
| `p2p/IceLite_FullAgentConnects` | PASS 27.85 us | PASS 32.71 us |
| `p2p/TurnPort_AllocateUdp` | PASS 9.65 us | PASS 13.39 us |
| `p2p/TurnPort_ChannelDataRelay` | PASS 755.1 ns | PASS 641.1 ns |
| `p2p/TcpSocket_Rfc4571Framing` | PASS 1.86 us | PASS 2.24 us |
| `p2p/TcpSocket_StunChannelDataFraming` | PASS 1.84 us | PASS 2.30 us |
| `security/Digest_Sha256_1200B` | PASS 459.2 ns | - |
| `security/Hmac_Sha1_100B` | PASS 162.0 ns | - |
| `security/CryptoRandom_16B` | PASS 207.8 ns | - |
| `security/Identity_EcdsaAndFingerprint` | PASS 19.28 us | - |
| `security/Fingerprint_Sha256` | PASS 470.9 ns | - |
| `security/Dtls_Handshake` | PASS 166.44 us | - |
| `security/Dtls_Record_1000B` | PASS 840.1 ns | - |
| `security/DtlsSrtp_ExportKeys` | PASS 606.1 ns | - |
| `security/Srtp_RtpRoundTrip_1200B_AesCmSha1_80` | PASS 1.09 us | - |
| `security/Srtp_RtpRoundTrip_1200B_AeadAes128Gcm` | PASS 399.3 ns | - |
| `security/Srtcp_RoundTrip_ReceiverReport` | PASS 197.4 ns | - |
| `security/Tls_Handshake` | PASS 164.38 us | - |
| `security/Tls_Record_1200B` | PASS 770.6 ns | - |
| `dc/SctpPacket_BuildParse_Sack_Data1000B` | PASS 4.08 us | - |
| `dc/Socket_Message1000B` | PASS 4.44 us | - |
| `dc/Socket_Connect` | PASS 3.92 us | - |
| `dc/SctpOverDtls_Message1000B` | PASS 6.05 us | - |
| `dc/DataChannel_OpenSendClose` | PASS 14.92 us | - |
| `dc/DataChannel_Message1000B` | PASS 5.78 us | - |
| `obs/Log_BelowRuntimeSeverity` | PASS 0.8 ns | PASS 0.7 ns |
| `obs/Event_NoSink` | PASS 0.3 ns | PASS 0.5 ns |
| `obs/Event_EventLog` | PASS 2.1 ns | PASS 2.6 ns |
| `obs/Histogram_Add` | PASS 3.7 ns | PASS 4.3 ns |
| `obs/MetricsRegistry_ExportJson` | PASS 3.23 us | PASS 4.07 us |
| `obs/ConnectionStats_Collect` | PASS 3.30 us | - |
| `obs/NackTracker_EventsOff` | PASS 174.0 ns | PASS 195.0 ns |
| `obs/NackTracker_EventsOn` | PASS 153.4 ns | PASS 195.3 ns |
| `obs/Fuzz_h264_depacketizer` | PASS 543.8 ns | - |
| `obs/Fuzz_packet_buffer` | PASS 10.63 us | - |
| `obs/Fuzz_red` | PASS 256.6 ns | - |
| `obs/Fuzz_rtcp` | PASS 670.5 ns | - |
| `obs/Fuzz_rtp_packet` | PASS 216.3 ns | - |
| `obs/Fuzz_stun` | PASS 9.73 us | - |
| `obs/Fuzz_tcp_framing` | PASS 4.06 us | - |
| `obs/Fuzz_transport_feedback` | PASS 565.6 ns | - |
| `obs/Fuzz_turn` | PASS 63.28 us | - |
| `obs/Fuzz_ulpfec` | PASS 7.69 us | - |
| `obs/Fuzz_dcsctp_packet` | PASS 373.0 ns | - |
| `obs/Fuzz_dcsctp_socket` | PASS 4.92 us | - |
| `obs/Fuzz_dtls_record` | PASS 82.31 us | - |
| `obs/Fuzz_dtls_transport` | PASS 74.75 us | - |
| `obs/Fuzz_sdp_parser` | PASS 17.32 us | - |
| `p2p/Sdp_ParseChromeOffer` | PASS 23.36 us | - |
| `p2p/Sdp_WriteChromeOffer` | PASS 7.33 us | - |
| `p2p/OfferAnswer_CreateOfferAndAnswer` | PASS 21.81 us | - |
| `p2p/Endpoint_CallSetup` | PASS 908.04 us | - |

