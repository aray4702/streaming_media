sm_add_library(core SOURCES
  core/metrics.cc
  core/event_log.cc
  core/logging.cc
  core/fft.cc
  core/rms_level.cc
  core/audio_frame_operations.cc
  core/dsp.cc
  core/rtp_packet.cc)

set(SM_SECTIONS core)

if(SM_WITH_TLS)
  # Connectivity (milestone 3): crypto and TLS through BoringSSL, libwebrtc's rtc_base/ interfaces (#984).
  include(${PROJECT_SOURCE_DIR}/cmake/SmBoringSSL.cmake)
  sm_add_boringssl()
  # SRTP (C5 part 1, #995): libsrtp 2.8.0 on BoringSSL, as libwebrtc M157 builds it.
  include(${PROJECT_SOURCE_DIR}/cmake/SmLibsrtp.cmake)
  sm_add_libsrtp()
  sm_add_library(tls DEPS sm::core SOURCES
    tls/message_digest.cc
    tls/boringssl_digest.cc
    tls/crypto_random.cc
    tls/boringssl_identity.cc
    tls/ssl_fingerprint.cc
    tls/boringssl_dtls.cc
    tls/srtp_session.cc)
  target_link_libraries(sm_tls PRIVATE sm_boringssl_ssl sm_libsrtp)
  target_compile_definitions(sm_tls PUBLIC SM_WITH_TLS=1)
  # Not in the sm::sm umbrella or the install export yet: it links BoringSSL's targets, which the export
  # set doesn't carry. Connectivity code links sm::tls itself until the package exports BoringSSL (#984).
endif()

# Connectivity (milestone 3): addresses and packet sockets for ICE, libwebrtc's p2p/ and rtc_base/ socket
# layer (#996). Plain C++ with no OS socket calls, so it builds everywhere; OS sockets come with C11/C12.
# Not in the sm::sm umbrella yet, like sm::tls: it joins when the connectivity API settles (#944).
sm_add_library(p2p DEPS sm::core SOURCES
  p2p/socket_address.cc
  p2p/async_packet_socket.cc
  p2p/dtls_utils.cc
  p2p/stun.cc)
if(SM_WITH_TLS)
  # DTLS over an ICE transport (C5 part 2, #1006): libwebrtc's p2p/dtls/dtls_transport on sm::tls.
  sm_add_library(p2p_dtls DEPS sm::p2p sm::tls SOURCES
    p2p/dtls_transport.cc)
endif()

if(SM_WITH_LOSS)
  sm_add_library(loss DEPS sm::core SOURCES
    loss/nack_tracker.cc
    loss/audio_nack_tracker.cc
    loss/rtx.cc
    loss/red.cc
    loss/keyframe_request.cc
    loss/rtcp_feedback.cc
    loss/ulpfec.cc
    loss/ulpfec_tables.cc
    loss/fec_controller.cc
    loss/fec_rate_table.cc)
  list(APPEND SM_SECTIONS loss)
endif()

if(SM_WITH_TIMING)
  sm_add_library(timing DEPS sm::loss SOURCES
    timing/packet_buffer.cc
    timing/rtcp_stats.cc
    timing/delay_manager.cc
    timing/neteq_delay.cc
    timing/neteq_expand.cc
    timing/time_stretch.cc
    timing/audio_jitter_buffer.cc
    timing/video_jitter_estimator.cc
    timing/vcm_timing.cc
    timing/frame_release.cc)
  list(APPEND SM_SECTIONS timing)
endif()

if(SM_WITH_CC)
  sm_add_library(cc DEPS sm::core SOURCES
    cc/transport_feedback.cc
    cc/delay_based_bwe.cc
    cc/aimd_rate_control.cc
    cc/loss_based_bwe_v2.cc
    cc/send_side_bandwidth_estimation.cc
    cc/send_side_bwe.cc
    cc/pacer.cc
    cc/pacing_controller.cc
    cc/probing.cc
    cc/bitrate_prober.cc
    cc/alr_detector.cc
    cc/bitrate_allocator.cc
    cc/bitrate_allocator_v2.cc
    cc/frame_dropper.cc
    cc/encoder_bitrate_adjuster.cc
    cc/congestion_window_pushback.cc
    cc/control_handler.cc
    cc/video_adaptation.cc
    cc/outage_monitor.cc)
  list(APPEND SM_SECTIONS cc)
endif()

if(SM_WITH_AUDIO)
  sm_add_library(audio DEPS sm::core SOURCES
    audio/hpf.cc
    audio/cascaded_biquad_filter.cc
    audio/vad.cc
    audio/agc2.cc
    audio/agc2_rnn_vad.cc
    audio/agc2_rnn_vad_weights.cc
    audio/sinc_resampler.cc
    audio/spl_resampler.cc
    audio/agc1.cc
    audio/agc1_vad.cc
    audio/agc1_vad_isac.cc
    audio/agc_manager.cc
    audio/clipping_predictor.cc
    audio/aec3_delay.cc
    audio/aec3_filter.cc
    audio/aec3_reverb.cc
    audio/aec3_analysis.cc
    audio/aec3_erle.cc
    audio/aec3_state.cc
    audio/aec3_suppression.cc
    audio/aec3.cc
    audio/transient_suppressor.cc
    audio/audio_level.cc
    audio/splitting_filter.cc
    audio/ns.cc
    audio/fft4g.cc
    audio/ooura_fft.cc)
  list(APPEND SM_SECTIONS audio)
  # Bit-exact with libwebrtc's float/double arithmetic (which Chromium builds
  # with -ffp-contract=off): no fused multiply-adds on FMA targets.
  set_source_files_properties(audio/agc1_vad.cc audio/agc1_vad_isac.cc
                              audio/fft4g.cc audio/ooura_fft.cc audio/ns.cc audio/aec3_filter.cc audio/aec3_reverb.cc
                              audio/aec3_analysis.cc audio/aec3_erle.cc audio/aec3_state.cc
                              audio/aec3_suppression.cc audio/aec3.cc PROPERTIES
    COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>")
endif()

if(SM_WITH_VIDEO)
  sm_add_library(video DEPS sm::core SOURCES
    video/frame.cc
    video/freeze_detector.cc
    video/codec_fallback.cc
    video/orientation.cc
    video/content_policy.cc
    video/frame_cadence_adapter.cc
    video/frame_scheduler.cc
    video/render_frames.cc
    video/quality_convergence.cc
    video/quality_scaler.cc
    video/video_stream_adapter.cc
    video/resource_adaptation_processor.cc)
  list(APPEND SM_SECTIONS video)
endif()

if(SM_WITH_SYNC)
  sm_add_library(sync DEPS sm::timing SOURCES
    sync/rtp_to_ntp.cc
    sync/stream_sync.cc
    sync/capture_time.cc
    sync/header_extensions.cc)
  list(APPEND SM_SECTIONS sync)
endif()

if(SM_WITH_RTP)
  sm_add_library(rtp DEPS sm::loss sm::timing sm::cc sm::sync sm::video SOURCES
    rtp/rtcp_engine.cc
    rtp/call_stats.cc
    rtp/rtcp_sdes_bye.cc
    rtp/h264.cc
    rtp/rtp_receiver.cc
    rtp/rtp_sender.cc
    rtp/send_side_controller.cc
    rtp/session.cc
    rtp/sps_vui_rewriter.cc
    rtp/video_send_controller.cc)
  list(APPEND SM_SECTIONS rtp)
endif()

# Umbrella target: link sm::sm to get every enabled section.
add_library(sm_all INTERFACE)
add_library(sm::sm ALIAS sm_all)
foreach(s ${SM_SECTIONS})
  target_link_libraries(sm_all INTERFACE sm::${s})
endforeach()
set(SM_SECTIONS ${SM_SECTIONS} PARENT_SCOPE)

# Install + package export: consumers use
#   find_package(streaming_media CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE sm::sm)   # or sm::audio, sm::cc, ...
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set_target_properties(sm_all PROPERTIES EXPORT_NAME sm)
# sm/sim (network emulator, test signals) is header-only and installed too,
# so applications can reuse it in their own integration tests.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/sm DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(_sm_targets sm_all)
foreach(s ${SM_SECTIONS})
  list(APPEND _sm_targets sm_${s})
endforeach()
install(TARGETS ${_sm_targets} EXPORT streaming_mediaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT streaming_mediaTargets NAMESPACE sm::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/streaming_media)
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/streaming_mediaConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/streaming_media)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/streaming_media)
