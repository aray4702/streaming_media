# Verification results

Produced by `tools/run_all.sh all`; regenerate with `tools/gen_docs.py`.

## Platforms

| Platform | Compiler | Runtime | Date | CTest |
|---|---|---|---|---|
| macOS arm64 (native) | Apple clang version 21.0.0 (clang-2100.3.34.2) | native | 2026-10-11T02:24:34Z | 100% tests passed out of 113 |
| WebAssembly (Node) | emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) 6.0.10-git | node v26.10.0 | 2026-10-11T02:25:40Z | 100% tests passed out of 113 |
| ASan+UBSan (Debug) | Apple clang version 21.0.0 (clang-2100.3.34.2) | native | 2026-10-11T02:28:39Z | 100% tests passed out of 106 |
| -fno-exceptions -fno-rtti | Apple clang version 21.0.0 (clang-2100.3.34.2) | native | 2026-10-11T02:28:51Z | 100% tests passed out of 106 |

CTest has one entry per test suite (one unit and one end-to-end suite per algorithm) plus one benchmark gate per section (`bench.<section>`, run with a short minimum time).

- **macOS arm64 (native)**: 213/213 test cases passed, 79/79 benchmarks passed.
- **WebAssembly (Node)**: 213/213 test cases passed, 79/79 benchmarks passed.
- **ASan+UBSan (Debug)**: 213/213 test cases passed.
- **-fno-exceptions -fno-rtti**: 213/213 test cases passed.

## Footprint (native Release, code size per section)

| Library | Code (`size` text) | Archive file |
|---|---|---|
| `sm_audio` | 62 KiB | 136 KiB |
| `sm_cc` | 74 KiB | 170 KiB |
| `sm_core` | 37 KiB | 76 KiB |
| `sm_loss` | 61 KiB | 134 KiB |
| `sm_sync` | 25 KiB | 61 KiB |
| `sm_timing` | 42 KiB | 104 KiB |
| `sm_video` | 52 KiB | 109 KiB |

Each section is a separate static library and can be turned off with `SM_WITH_<SECTION>=OFF`; the linker keeps only what is used.

## Benchmarks

| Benchmark | macOS arm64 (native) | WebAssembly (Node) |
|---|---|---|
| `core/Histogram_Add` | PASS 3.4 ns | PASS 5.8 ns |
| `core/RealFft_256` | PASS 638.2 ns | PASS 870.3 ns |
| `core/RtpPacket_SerializeParse` | PASS 78.4 ns | PASS 362.4 ns |
| `loss/NackTracker_PacketWith5PctLoss` | PASS 30.8 ns | PASS 35.4 ns |
| `loss/RtxSender_StoreAndResend` | PASS 32.1 ns | PASS 49.4 ns |
| `loss/FecEncode_10pkt_frame_50pct` | PASS 426.9 ns | PASS 482.5 ns |
| `loss/FecDecode_RecoverOne` | PASS 111.5 ns | PASS 118.3 ns |
| `loss/RedEncodeParse_d2` | PASS 481.9 ns | PASS 372.4 ns |
| `loss/Plc_Conceal10ms_16k` | PASS 434x RT | PASS 455x RT |
| `loss/KeyframeRequester_Frame` | PASS 87.5 ns | PASS 61.1 ns |
| `loss/TemporalLayers_ScheduleAndFilter` | PASS 19.6 ns | PASS 13.5 ns |
| `loss/ProtectionController_Update` | PASS 364.2 ns | PASS 327.5 ns |
| `loss/Q_NackRtx_5pct_rtt80` | PASS (quality) | PASS (quality) |
| `loss/Q_Fec_5pct_rtt300` | PASS (quality) | PASS (quality) |
| `loss/Q_Red_10pct` | PASS (quality) | PASS (quality) |
| `loss/Q_Plc_10pct_speech` | PASS (quality) | PASS (quality) |
| `loss/Q_Keyframe_2pct_frame_loss` | PASS (quality) | PASS (quality) |
| `loss/Q_TemporalLayers_5pct` | PASS (quality) | PASS (quality) |
| `loss/Q_Protection_adaptive_rtt300` | PASS (quality) | PASS (quality) |
| `timing/PacketBuffer_InsertAssemble` | PASS 152.3 ns | PASS 259.7 ns |
| `timing/ReceiveStatistics_OnPacket` | PASS 2.5 ns | PASS 2.5 ns |
| `timing/DelayManager_Update` | PASS 142.1 ns | PASS 163.9 ns |
| `timing/TimeStretch_Accelerate30ms_16k` | PASS 3021x RT | PASS 2532x RT |
| `timing/AudioJitterBuffer_10msPull_16k` | PASS 78118x RT | PASS 60861x RT |
| `timing/VideoJitterEstimator_Update` | PASS 24.5 ns | PASS 37.7 ns |
| `timing/DriftCompensator_10ms_16k` | PASS 33123x RT | PASS 26466x RT |
| `timing/Q_AudioJitterBuffer_jitter25_loss2` | PASS (quality) | PASS (quality) |
| `timing/Q_TimeStretch_stall600ms` | PASS (quality) | PASS (quality) |
| `timing/Q_VideoJitter_late_frames` | PASS (quality) | PASS (quality) |
| `timing/Q_ClockDrift_300ppm` | PASS (quality) | PASS (quality) |
| `cc/TransportFeedback_Build100_SerializeParse` | PASS 36.8 ns | PASS 33.3 ns |
| `cc/Gcc_OnFeedback_per_packet` | PASS 145.2 ns | PASS 112.5 ns |
| `cc/Pacer_EnqueueProcess` | PASS 23.5 ns | PASS 25.4 ns |
| `cc/BitrateAllocator_5streams` | PASS 614.1 ns | PASS 372.3 ns |
| `cc/FrameDropper_and_Overshoot` | PASS 24.5 ns | PASS 26.6 ns |
| `cc/Degradation_and_CpuDetector` | PASS 4.2 ns | PASS 5.5 ns |
| `cc/LayerSelector_frame` | PASS 4.3 ns | PASS 3.8 ns |
| `cc/Q_Gcc_capacity_steps` | PASS (quality) | PASS (quality) |
| `cc/Q_LossBwe_15pct` | PASS (quality) | PASS (quality) |
| `cc/Q_Pacer_audio_delay` | PASS (quality) | PASS (quality) |
| `cc/Q_Probing_rampup_5mbps` | PASS (quality) | PASS (quality) |
| `cc/Q_Allocator_low_bandwidth` | PASS (quality) | PASS (quality) |
| `cc/Q_FrameDropper_overshoot160` | PASS (quality) | PASS (quality) |
| `cc/Q_Adaptation_bandwidth_drop` | PASS (quality) | PASS (quality) |
| `cc/Q_Outage_recovery` | PASS (quality) | PASS (quality) |
| `audio/HighPassFilter_10ms` | PASS 11382x RT | PASS 22723x RT |
| `audio/Vad_10ms` | PASS 8853x RT | PASS 6723x RT |
| `audio/NoiseSuppressor_10ms` | PASS 2838x RT | PASS 2063x RT |
| `audio/Agc_10ms` | PASS 35524x RT | PASS 26880x RT |
| `audio/EchoDelayEstimator_10ms` | PASS 246x RT | PASS 170x RT |
| `audio/EchoCanceller_10ms_80ms_tail` | PASS 98x RT | PASS 69x RT |
| `audio/TransientSuppressor_10ms` | PASS 85146x RT | PASS 87791x RT |
| `audio/DtxController_10ms` | PASS 6191x RT | PASS 4739x RT |
| `audio/ComfortNoise_10ms` | PASS 4859x RT | PASS 4068x RT |
| `audio/AudioLevel_and_ActiveSpeaker_20ms` | PASS 117501x RT | PASS 121963x RT |
| `audio/Mixer_8sources_10ms` | PASS 9261x RT | PASS 7871x RT |
| `audio/Resampler_48k_to_16k_10ms` | PASS 946x RT | PASS 857x RT |
| `audio/Resampler_44k1_to_48k_10ms` | PASS 2648x RT | PASS 1397x RT |
| `audio/FullCaptureChain_10ms` | PASS 100x RT | PASS 73x RT |
| `audio/Q_EchoCanceller_full_duplex_call` | PASS (quality) | PASS (quality) |
| `audio/Q_EchoDelay_route_change` | PASS (quality) | PASS (quality) |
| `audio/Q_NoiseSuppressor_7dB_snr` | PASS (quality) | PASS (quality) |
| `video/FreezeDetector_frame` | PASS 3.9 ns | PASS 4.0 ns |
| `video/CodecFallback_frame` | PASS 3.6 ns | PASS 3.1 ns |
| `video/Orientation_rotate90_720p` | PASS 60x RT | PASS 70x RT |
| `video/FrameChangeDetector_1080p` | PASS 41x RT | PASS 38x RT |
| `video/CodecNegotiation_offer10` | PASS 590.0 ns | PASS 697.0 ns |
| `video/Scale_720p_to_360p` | PASS 20x RT | PASS 15x RT |
| `video/Scale_360p_to_720p` | PASS 19x RT | PASS 8x RT |
| `video/FrameScheduler_frame` | PASS 23.2 ns | PASS 21.0 ns |
| `video/QualityScaler_frame` | PASS 1.7 ns | PASS 2.1 ns |
| `video/Q_Scaler_psnr_720p_via_360p` | PASS (quality) | PASS (quality) |
| `video/Q_ScreenShare_encode_ratio` | PASS (quality) | PASS (quality) |
| `sync/RtpToNtp_UpdateAndEstimate` | PASS 37.3 ns | PASS 42.4 ns |
| `sync/RtpToNtp_Estimate` | PASS 1.3 ns | PASS 4.0 ns |
| `sync/StreamSync_Update` | PASS 11.5 ns | PASS 23.8 ns |
| `sync/AbsCaptureTime_SenderAndInterpolator` | PASS 3.2 ns | PASS 3.7 ns |
| `sync/HeaderExtensions_WriteParse4` | PASS 1.06 us | PASS 726.1 ns |
| `sync/RtpDemuxer_by_ssrc` | PASS 87.2 ns | PASS 63.5 ns |

