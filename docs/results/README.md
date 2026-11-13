# Verification results

Produced by `tools/run_all.sh all`; regenerate with `tools/gen_docs.py`.

## Platforms

| Platform | Compiler | Runtime | Date | CTest |
|---|---|---|---|---|
| macOS arm64 (native) | Apple clang version 21.0.0 (clang-2100.4.1.1) | native | 2026-11-13T20:03:35Z | 100% tests passed out of 631 |
| WebAssembly (Node) | emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) 6.0.10-git | node v26.10.0 | 2026-11-13T20:08:17Z | 100% tests passed out of 469 |
| ASan+UBSan (Debug) | Apple clang version 21.0.0 (clang-2100.4.1.1) | native | 2026-11-13T20:32:06Z | 100% tests passed out of 619 |
| -fno-exceptions -fno-rtti | Apple clang version 21.0.0 (clang-2100.4.1.1) | native | 2026-11-13T20:36:30Z | 100% tests passed out of 541 |

CTest has one entry per test suite (one unit and one end-to-end suite per algorithm) plus one benchmark gate per section (`bench.<section>`, run with a short minimum time).

- **macOS arm64 (native)**: 2990/2990 test cases passed, 139/139 benchmarks passed.
- **WebAssembly (Node)**: 2074/2074 test cases passed, 100/100 benchmarks passed.
- **ASan+UBSan (Debug)**: 2990/2990 test cases passed.
- **-fno-exceptions -fno-rtti**: 2438/2438 test cases passed.

## Footprint (native Release, code size per section)

| Library | Code (`size` text) | Archive file |
|---|---|---|
| `sm_audio` | 478 KiB | 843 KiB |
| `sm_cc` | 210 KiB | 420 KiB |
| `sm_core` | 40 KiB | 86 KiB |
| `sm_dcsctp` | 324 KiB | 1010 KiB |
| `sm_device` | 26 KiB | 82 KiB |
| `sm_libsrtp` | 44 KiB | 110 KiB |
| `sm_loss` | 100 KiB | 204 KiB |
| `sm_mbedtls` | 669 KiB | 1356 KiB |
| `sm_p2p` | 597 KiB | 1541 KiB |
| `sm_p2p_dtls` | 26 KiB | 85 KiB |
| `sm_p2p_tls` | 21 KiB | 68 KiB |
| `sm_pc` | 60 KiB | 174 KiB |
| `sm_pc_datachannel` | 34 KiB | 89 KiB |
| `sm_pc_endpoint` | 137 KiB | 310 KiB |
| `sm_pc_sdp` | 458 KiB | 880 KiB |
| `sm_rtp` | 287 KiB | 604 KiB |
| `sm_sctp` | 49 KiB | 144 KiB |
| `sm_sync` | 47 KiB | 104 KiB |
| `sm_timing` | 165 KiB | 296 KiB |
| `sm_tls` | 51 KiB | 142 KiB |
| `sm_tls_mbedtls` | 51 KiB | 133 KiB |
| `sm_video` | 103 KiB | 215 KiB |
| `sm_videotoolbox` | 12 KiB | 33 KiB |

Each section is a separate static library and can be turned off with `SM_WITH_<SECTION>=OFF`; the linker keeps only what is used.

## Benchmarks

| Benchmark | macOS arm64 (native) | WebAssembly (Node) |
|---|---|---|
| `core/Histogram_Add` | PASS 3.8 ns | PASS 4.1 ns |
| `core/RealFft_256` | PASS 622.8 ns | PASS 946.6 ns |
| `core/RtpPacket_SerializeParse` | PASS 86.7 ns | PASS 355.3 ns |
| `loss/NackTracker_PacketWith5PctLoss` | PASS 145.0 ns | PASS 191.2 ns |
| `loss/RtxSender_StoreAndResend` | PASS 74.6 ns | PASS 91.2 ns |
| `loss/FecEncode_10pkt_frame_50pct` | PASS 150.8 ns | PASS 1.01 us |
| `loss/FecDecode_RecoverOne` | PASS 231.9 ns | PASS 442.4 ns |
| `loss/RedEncodeParse_d2` | PASS 486.3 ns | PASS 386.4 ns |
| `loss/KeyframeRequester_Frame` | PASS 89.6 ns | PASS 61.2 ns |
| `loss/FecControllerDefault_UpdateFecRates` | PASS 20.5 ns | PASS 28.8 ns |
| `loss/Q_NackRtx_5pct_rtt80` | PASS (quality) | PASS (quality) |
| `loss/Q_Fec_5pct_rtt300` | PASS (quality) | PASS (quality) |
| `loss/Q_Red_10pct` | PASS (quality) | PASS (quality) |
| `loss/Q_Keyframe_2pct_frame_loss` | PASS (quality) | PASS (quality) |
| `loss/Q_Protection_adaptive_rtt300` | PASS (quality) | PASS (quality) |
| `timing/PacketBuffer_InsertAssemble` | PASS 157.7 ns | PASS 145.6 ns |
| `timing/ReceiveStatistics_OnPacket` | PASS 4.0 ns | PASS 4.2 ns |
| `timing/DelayManager_Update` | PASS 145.3 ns | PASS 159.4 ns |
| `timing/TimeStretch_Accelerate30ms_16k` | PASS 3008x RT | PASS 2604x RT |
| `timing/AudioJitterBuffer_10msPull_16k` | PASS 27818x RT | PASS 7519x RT |
| `timing/VideoJitterEstimator_Update` | PASS 24.6 ns | PASS 37.5 ns |
| `timing/Q_AudioJitterBuffer_jitter25_loss2` | PASS (quality) | PASS (quality) |
| `timing/Q_TimeStretch_stall600ms` | PASS (quality) | PASS (quality) |
| `timing/Q_VideoJitter_late_frames` | PASS (quality) | PASS (quality) |
| `timing/Q_Plc_10pct_speech` | PASS (quality) | PASS (quality) |
| `cc/TransportFeedback_Build100_SerializeParse` | PASS 55.8 ns | PASS 51.7 ns |
| `cc/Gcc_OnFeedback_per_packet` | PASS 185.1 ns | PASS 213.3 ns |
| `cc/Pacer_EnqueueProcess` | PASS 96.6 ns | PASS 93.6 ns |
| `cc/BitrateAllocator_5streams` | PASS 637.2 ns | PASS 365.9 ns |
| `cc/FrameDropper_and_Overshoot` | PASS 305.9 ns | PASS 331.8 ns |
| `cc/CpuOveruseDetector_PerFrame` | PASS 32.0 ns | PASS 46.6 ns |
| `cc/Q_Gcc_capacity_steps` | PASS (quality) | PASS (quality) |
| `cc/Q_LossBwe_15pct` | PASS (quality) | PASS (quality) |
| `cc/Q_Pacer_audio_delay` | PASS (quality) | PASS (quality) |
| `cc/Q_Probing_rampup_5mbps` | PASS (quality) | PASS (quality) |
| `cc/Q_Allocator_low_bandwidth` | PASS (quality) | PASS (quality) |
| `cc/Q_FrameDropper_overshoot160` | PASS (quality) | PASS (quality) |
| `cc/Q_Outage_recovery` | PASS (quality) | PASS (quality) |
| `audio/Vad_10ms` | PASS 15390x RT | PASS 12536x RT |
| `audio/SplittingFilter_48k_10ms` | PASS 6603x RT | PASS 1892x RT |
| `audio/TransientSuppressor_10ms` | PASS 85734x RT | PASS 90937x RT |
| `audio/AudioLevel_20ms` | PASS 91601x RT | PASS 62538x RT |
| `audio/HighPassFilter_10ms` | PASS 9216x RT | PASS 6718x RT |
| `audio/NoiseSuppressor_10ms` | PASS 3999x RT | PASS 2408x RT |
| `audio/Agc1_10ms` | PASS 748x RT | PASS 625x RT |
| `audio/Agc2_10ms` | PASS 324x RT | PASS 238x RT |
| `audio/Resampler_48k_to_16k_10ms` | PASS 6835x RT | PASS 3184x RT |
| `audio/Resampler_44k1_to_48k_10ms` | PASS 2288x RT | PASS 1068x RT |
| `audio/SplResampler_48k_to_16k_10ms` | PASS 3685x RT | PASS 3450x RT |
| `audio/Mixer_6sources_48k_10ms` | PASS 7166x RT | PASS 4976x RT |
| `audio/Q_EchoCanceller_full_duplex_call` | PASS (quality) | PASS (quality) |
| `audio/Q_EchoDelay_route_change` | PASS (quality) | PASS (quality) |
| `audio/Q_NoiseSuppressor_7dB_snr` | PASS (quality) | PASS (quality) |
| `audio/Q_Agc_quiet_then_loud_talker` | PASS (quality) | PASS (quality) |
| `audio/Q_Mixer_5_loud_talkers` | PASS (quality) | PASS (quality) |
| `video/FreezeDetector_frame` | PASS 3.6 ns | PASS 4.1 ns |
| `video/CodecFallback_frame` | PASS 1.6 ns | PASS 1.1 ns |
| `video/Orientation_rotate90_720p` | PASS 52x RT | PASS 70x RT |
| `video/FrameCadenceAdapter_frame` | PASS 39.5 ns | PASS 46.8 ns |
| `video/FrameScheduler_frame` | PASS 29.2 ns | PASS 23.0 ns |
| `video/QualityScaler_frame` | PASS 5.6 ns | PASS 7.2 ns |
| `video/SimulcastRateAllocator_3layers_update` | PASS 70.0 ns | PASS 46.0 ns |
| `video/Q_ScreenShare_encode_ratio` | PASS (quality) | PASS (quality) |
| `sync/RtpToNtp_UpdateAndEstimate` | PASS 39.3 ns | PASS 39.2 ns |
| `sync/RtpToNtp_Estimate` | PASS 2.2 ns | PASS 2.0 ns |
| `sync/StreamSync_Update` | PASS 5.8 ns | PASS 5.8 ns |
| `sync/AbsCaptureTime_SenderAndInterpolator` | PASS 5.7 ns | PASS 6.3 ns |
| `sync/HeaderExtensions_WriteParse4` | PASS 983.8 ns | PASS 697.1 ns |
| `sync/RtpDemuxer_by_ssrc` | PASS 78.7 ns | PASS 62.4 ns |
| `rtp/SdesCname_SerializeParse` | PASS 140.5 ns | PASS 108.5 ns |
| `rtp/SimulcastSend_3layers_frame` | PASS 181.3 ns | PASS 166.2 ns |
| `rtp/Bye_SerializeParse` | PASS 136.9 ns | PASS 91.3 ns |
| `rtp/RtcpEngine_CompoundReportBuildAndParse` | PASS 913.1 ns | PASS 754.2 ns |
| `rtp/RtcpEngine_NackPliFeedbackBuildAndParse` | PASS 1.21 us | PASS 976.3 ns |
| `rtp/RtpSender_SendVideoFramePaced` | PASS 13.17 us | PASS 17.50 us |
| `rtp/RtpReceiver_ReceivePacket` | PASS 790.4 ns | PASS 1.14 us |
| `rtp/Session_VideoFrameEndToEnd` | PASS 57.08 us | PASS 69.99 us |
| `rtp/Q_Session_Video_5pct_loss` | PASS (quality) | PASS (quality) |
| `rtp/H264_Keyframe1080p_PacketizeDepacketize` | PASS 86.53 us | PASS 162.73 us |
| `rtp/H264_DeltaFrame1080p_PacketizeDepacketize` | PASS 12.10 us | PASS 22.16 us |
| `p2p/Stun_ParseIceRequest` | PASS 494.2 ns | PASS 421.6 ns |
| `p2p/Stun_BuildBindingResponse` | PASS 612.8 ns | PASS 492.7 ns |
| `p2p/UdpPort_GatherHostAndSrflx` | PASS 3.49 us | PASS 4.36 us |
| `p2p/Connection_CheckRoundTrip` | PASS 4.33 us | PASS 6.54 us |
| `p2p/PortAllocator_GatherSession` | PASS 7.01 us | PASS 8.02 us |
| `p2p/IceController_SortPingPrune` | PASS 373.6 ns | PASS 273.6 ns |
| `p2p/IceAgent_ConnectBehindNats` | PASS 64.21 us | PASS 73.30 us |
| `p2p/IceAgent_SendPacket` | PASS 349.3 ns | PASS 394.3 ns |
| `p2p/IceLite_FullAgentConnects` | PASS 27.14 us | PASS 33.53 us |
| `p2p/TurnPort_AllocateUdp` | PASS 9.37 us | PASS 13.67 us |
| `p2p/TurnPort_ChannelDataRelay` | PASS 791.9 ns | PASS 656.7 ns |
| `p2p/TcpSocket_Rfc4571Framing` | PASS 1.85 us | PASS 2.28 us |
| `p2p/TcpSocket_StunChannelDataFraming` | PASS 1.83 us | PASS 2.29 us |
| `security/Digest_Sha256_1200B` | PASS 423.6 ns | - |
| `security/Hmac_Sha1_100B` | PASS 156.9 ns | - |
| `security/CryptoRandom_16B` | PASS 207.3 ns | - |
| `security/Identity_EcdsaAndFingerprint` | PASS 19.15 us | - |
| `security/Fingerprint_Sha256` | PASS 448.8 ns | - |
| `security/Dtls_Handshake` | PASS 164.28 us | - |
| `security/Dtls_Record_1000B` | PASS 801.4 ns | - |
| `security/DtlsSrtp_ExportKeys` | PASS 617.7 ns | - |
| `security/Srtp_RtpRoundTrip_1200B_AesCmSha1_80` | PASS 1.06 us | - |
| `security/Srtp_RtpRoundTrip_1200B_AeadAes128Gcm` | PASS 391.2 ns | - |
| `security/Srtcp_RoundTrip_ReceiverReport` | PASS 197.8 ns | - |
| `security/Tls_Handshake` | PASS 161.49 us | - |
| `security/Tls_Record_1200B` | PASS 746.2 ns | - |
| `dc/SctpPacket_BuildParse_Sack_Data1000B` | PASS 355.6 ns | - |
| `dc/Socket_Message1000B` | PASS 792.3 ns | - |
| `dc/Socket_Connect` | PASS 3.21 us | - |
| `dc/SctpOverDtls_Message1000B` | PASS 2.23 us | - |
| `dc/DataChannel_OpenSendClose` | PASS 10.42 us | - |
| `dc/DataChannel_Message1000B` | PASS 2.31 us | - |
| `obs/Log_BelowRuntimeSeverity` | PASS 0.7 ns | PASS 0.6 ns |
| `obs/Event_NoSink` | PASS 0.2 ns | PASS 0.5 ns |
| `obs/Event_EventLog` | PASS 2.2 ns | PASS 2.4 ns |
| `obs/Histogram_Add` | PASS 3.9 ns | PASS 4.4 ns |
| `obs/MetricsRegistry_ExportJson` | PASS 3.11 us | PASS 4.12 us |
| `obs/ConnectionStats_Collect` | PASS 3.17 us | - |
| `obs/NackTracker_EventsOff` | PASS 156.2 ns | PASS 195.3 ns |
| `obs/NackTracker_EventsOn` | PASS 158.5 ns | PASS 194.3 ns |
| `obs/Fuzz_h264_depacketizer` | PASS 565.9 ns | - |
| `obs/Fuzz_packet_buffer` | PASS 10.52 us | - |
| `obs/Fuzz_red` | PASS 244.1 ns | - |
| `obs/Fuzz_rtcp` | PASS 596.4 ns | - |
| `obs/Fuzz_rtp_packet` | PASS 212.4 ns | - |
| `obs/Fuzz_stun` | PASS 9.26 us | - |
| `obs/Fuzz_tcp_framing` | PASS 4.03 us | - |
| `obs/Fuzz_transport_feedback` | PASS 568.1 ns | - |
| `obs/Fuzz_turn` | PASS 68.95 us | - |
| `obs/Fuzz_ulpfec` | PASS 7.24 us | - |
| `obs/Fuzz_dcsctp_packet` | PASS 227.5 ns | - |
| `obs/Fuzz_dcsctp_socket` | PASS 3.42 us | - |
| `obs/Fuzz_dtls_record` | PASS 79.27 us | - |
| `obs/Fuzz_dtls_transport` | PASS 73.51 us | - |
| `obs/Fuzz_sdp_parser` | PASS 16.75 us | - |
| `p2p/Sdp_ParseChromeOffer` | PASS 23.53 us | - |
| `p2p/Sdp_WriteChromeOffer` | PASS 7.19 us | - |
| `p2p/OfferAnswer_CreateOfferAndAnswer` | PASS 21.37 us | - |
| `p2p/Endpoint_CallSetup` | PASS 899.68 us | - |

