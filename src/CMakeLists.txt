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
  # Connectivity (milestone 3): crypto and TLS, libwebrtc's rtc_base/ interfaces (#984), on BoringSSL as libwebrtc
  # M157 builds, or on Mbed TLS for microcontrollers (SM_TLS_BACKEND=mbedtls, C1 part 3, #998).
  # sm_add_tls_backend(<name> <boringssl|mbedtls> [SRTP]) builds sm_<name> on that backend; SRTP adds SrtpSession on
  # libsrtp (C5 part 1, #995), which only one sm::tls build per binary can carry (libsrtp's C symbols).
  # The modules are included here, not in the function: enable_language(C) must run at directory scope.
  include(${PROJECT_SOURCE_DIR}/cmake/SmBoringSSL.cmake)
  include(${PROJECT_SOURCE_DIR}/cmake/SmMbedTLS.cmake)
  include(${PROJECT_SOURCE_DIR}/cmake/SmLibsrtp.cmake)
  function(sm_add_tls_backend name backend)
    cmake_parse_arguments(ARG "SRTP" "" "" ${ARGN})
    set(srcs tls/message_digest.cc tls/crypto_random.cc tls/ssl_fingerprint.cc)
    if(backend STREQUAL "boringssl")
      sm_add_boringssl()
      list(APPEND srcs tls/boringssl_digest.cc tls/boringssl_identity.cc tls/boringssl_dtls.cc tls/boringssl_tls.cc)
    else()
      sm_add_mbedtls()
      list(APPEND srcs tls/mbedtls_digest.cc tls/mbedtls_identity.cc tls/mbedtls_dtls.cc tls/mbedtls_tls.cc)
    endif()
    if(ARG_SRTP)
      if(backend STREQUAL "boringssl")
        sm_add_libsrtp()
      else()
        sm_add_libsrtp(MBEDTLS)
      endif()
      list(APPEND srcs tls/srtp_session.cc)
    endif()
    sm_add_library(${name} DEPS sm::core SOURCES ${srcs})
    if(backend STREQUAL "boringssl")
      target_link_libraries(sm_${name} PRIVATE sm_boringssl_ssl)
    else()
      target_link_libraries(sm_${name} PRIVATE sm_mbedtls)
      target_compile_definitions(sm_${name} PUBLIC SM_TLS_BACKEND_MBEDTLS=1)
    endif()
    if(ARG_SRTP)
      target_link_libraries(sm_${name} PRIVATE sm_libsrtp)
    endif()
    target_compile_definitions(sm_${name} PUBLIC SM_WITH_TLS=1)
  endfunction()

  sm_add_tls_backend(tls ${SM_TLS_BACKEND} SRTP)
  # Not in the sm::sm umbrella or the install export yet: it links the TLS library's targets, which the export
  # set doesn't carry. Connectivity code links sm::tls itself until the package exports them (#984).
  if(SM_BUILD_TESTS AND SM_TLS_CROSS_TEST)
    # The other backend, for its own unit tests and the BoringSSL <-> mbedTLS cross test (#998): sm::tls_mbedtls
    # (or sm::tls_boringssl when sm::tls is on mbedTLS), without SrtpSession.
    if(SM_TLS_BACKEND STREQUAL "boringssl")
      set(SM_TLS_OTHER_BACKEND mbedtls)
    else()
      set(SM_TLS_OTHER_BACKEND boringssl)
    endif()
    set(SM_TLS_OTHER_BACKEND ${SM_TLS_OTHER_BACKEND} PARENT_SCOPE)
    sm_add_tls_backend(tls_${SM_TLS_OTHER_BACKEND} ${SM_TLS_OTHER_BACKEND})
  endif()
endif()

# Connectivity (milestone 3): addresses and packet sockets for ICE, libwebrtc's p2p/ and rtc_base/ socket
# layer (#996). Plain C++ with no OS socket calls, so it builds everywhere; OS sockets come with C11/C12.
# Not in the sm::sm umbrella yet, like sm::tls: it joins when the connectivity API settles (#944).
sm_add_library(p2p DEPS sm::core SOURCES
  p2p/socket_address.cc
  p2p/async_packet_socket.cc
  p2p/async_tcp_socket.cc
  p2p/dtls_utils.cc
  p2p/stun.cc
  p2p/stun_request.cc
  p2p/network.cc
  p2p/candidate.cc
  p2p/transport_description.cc
  p2p/port.cc
  p2p/stun_port.cc
  p2p/turn_port.cc
  p2p/connection.cc
  p2p/network_manager.cc
  p2p/port_allocator.cc
  p2p/basic_port_allocator.cc
  p2p/ice_transport_internal.cc
  p2p/ice_switch_reason.cc
  p2p/basic_ice_controller.cc
  p2p/wrapping_active_ice_controller.cc
  p2p/ice_lite_agent.cc
  p2p/p2p_transport_channel.cc)
if(SM_WITH_TLS)
  # STUN's crypto hooks on BoringSSL (#997): InstallStunCryptoFromTls().
  # The TLS client socket under TURN over TLS (C4 part 3, #1035): InstallSslAdapterFromTls().
  sm_add_library(p2p_tls DEPS sm::p2p sm::tls SOURCES
    p2p/ssl_adapter_tls.cc
    p2p/stun_crypto_tls.cc)
  # The session description model and SDP parse/write (C6 part 1, #1062): libwebrtc's pc/session_description,
  # media/base/{codec,stream_params,rid_description}, pc/simulcast_*, api/jsep and api/webrtc_sdp. Offer/answer
  # generation (C6 part 2, #1063): pc/media_session, pc/codec_vendor, pc/sdp_payload_type_suggester and
  # p2p/base/transport_description_factory. On sm::tls for the
  # DTLS fingerprint (SslFingerprint) and CreateRandomString. Not in the sm::sm umbrella yet, like sm::p2p (#944).
  sm_add_library(pc_sdp DEPS sm::p2p sm::tls SOURCES
    pc/codec.cc
    pc/codec_vendor.cc
    pc/jsep.cc
    pc/media_protocol_names.cc
    pc/media_session.cc
    pc/sdp_payload_type_suggester.cc
    pc/session_description.cc
    pc/simulcast_description.cc
    pc/stream_params.cc
    pc/transport_description_factory.cc
    pc/webrtc_sdp.cc)
  # DTLS over an ICE transport (C5 part 2, #1006): libwebrtc's p2p/dtls/dtls_transport on sm::tls.
  sm_add_library(p2p_dtls DEPS sm::p2p sm::tls SOURCES
    p2p/dtls_transport.cc)
endif()

if(SM_WITH_DATACHANNEL)
  # Data channels (C7): libwebrtc M157's dcsctp, net/dcsctp/ (compare/webrtc/reference/net/dcsctp/). Part 1 (#1032):
  # the packet layer (SCTP packets, chunks, parameters, error causes, CRC-32C) and common/. Part 2 (#1060): the socket
  # (public/, socket/, rx/, tx/, timer/), with timeouts driven by Process(now) (timer/process_timeout.h) in place of
  # task_queue_timeout. Plain C++ that reads and writes bytes; it needs nothing from ICE or DTLS. Not in the sm::sm umbrella yet, like sm::p2p (#944).
  sm_add_library(dcsctp DEPS sm::core SOURCES
    dcsctp/packet/chunk/abort_chunk.cc
    dcsctp/packet/chunk/chunk.cc
    dcsctp/packet/chunk/cookie_ack_chunk.cc
    dcsctp/packet/chunk/cookie_echo_chunk.cc
    dcsctp/packet/chunk/data_chunk.cc
    dcsctp/packet/chunk/error_chunk.cc
    dcsctp/packet/chunk/forward_tsn_chunk.cc
    dcsctp/packet/chunk/heartbeat_ack_chunk.cc
    dcsctp/packet/chunk/heartbeat_request_chunk.cc
    dcsctp/packet/chunk/idata_chunk.cc
    dcsctp/packet/chunk/iforward_tsn_chunk.cc
    dcsctp/packet/chunk/init_ack_chunk.cc
    dcsctp/packet/chunk/init_chunk.cc
    dcsctp/packet/chunk/reconfig_chunk.cc
    dcsctp/packet/chunk/sack_chunk.cc
    dcsctp/packet/chunk/shutdown_ack_chunk.cc
    dcsctp/packet/chunk/shutdown_chunk.cc
    dcsctp/packet/chunk/shutdown_complete_chunk.cc
    dcsctp/packet/chunk_validators.cc
    dcsctp/packet/crc32c.cc
    dcsctp/packet/error_cause/cookie_received_while_shutting_down_cause.cc
    dcsctp/packet/error_cause/error_cause.cc
    dcsctp/packet/error_cause/invalid_mandatory_parameter_cause.cc
    dcsctp/packet/error_cause/invalid_stream_identifier_cause.cc
    dcsctp/packet/error_cause/missing_mandatory_parameter_cause.cc
    dcsctp/packet/error_cause/no_user_data_cause.cc
    dcsctp/packet/error_cause/out_of_resource_error_cause.cc
    dcsctp/packet/error_cause/protocol_violation_cause.cc
    dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause.cc
    dcsctp/packet/error_cause/stale_cookie_error_cause.cc
    dcsctp/packet/error_cause/unrecognized_chunk_type_cause.cc
    dcsctp/packet/error_cause/unrecognized_parameter_cause.cc
    dcsctp/packet/error_cause/unresolvable_address_cause.cc
    dcsctp/packet/error_cause/user_initiated_abort_cause.cc
    dcsctp/packet/parameter/add_incoming_streams_request_parameter.cc
    dcsctp/packet/parameter/add_outgoing_streams_request_parameter.cc
    dcsctp/packet/parameter/forward_tsn_supported_parameter.cc
    dcsctp/packet/parameter/heartbeat_info_parameter.cc
    dcsctp/packet/parameter/incoming_ssn_reset_request_parameter.cc
    dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter.cc
    dcsctp/packet/parameter/parameter.cc
    dcsctp/packet/parameter/reconfiguration_response_parameter.cc
    dcsctp/packet/parameter/ssn_tsn_reset_request_parameter.cc
    dcsctp/packet/parameter/state_cookie_parameter.cc
    dcsctp/packet/parameter/supported_extensions_parameter.cc
    dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.cc
    dcsctp/packet/sctp_packet.cc
    dcsctp/packet/tlv_trait.cc
    dcsctp/public/dcsctp_handover_state.cc
    dcsctp/public/dcsctp_socket_factory.cc
    dcsctp/rx/data_tracker.cc
    dcsctp/rx/interleaved_reassembly_streams.cc
    dcsctp/rx/reassembly_queue.cc
    dcsctp/rx/traditional_reassembly_streams.cc
    dcsctp/socket/callback_deferrer.cc
    dcsctp/socket/dcsctp_socket.cc
    dcsctp/socket/heartbeat_handler.cc
    dcsctp/socket/packet_sender.cc
    dcsctp/socket/state_cookie.cc
    dcsctp/socket/stream_reset_handler.cc
    dcsctp/socket/transmission_control_block.cc
    dcsctp/timer/process_timeout.cc
    dcsctp/timer/timer.cc
    dcsctp/tx/outstanding_data.cc
    dcsctp/tx/retransmission_error_counter.cc
    dcsctp/tx/retransmission_queue.cc
    dcsctp/tx/retransmission_timeout.cc
    dcsctp/tx/rr_send_queue.cc
    dcsctp/tx/stream_scheduler.cc)
  target_compile_definitions(sm_dcsctp PUBLIC SM_WITH_DATACHANNEL=1)
  # libwebrtc names constructor parameters after the members they set, and its RFC diagrams end lines with a
  # backslash: dcsctp builds without -Wshadow and -Wcomment.
  if(NOT MSVC)
    target_compile_options(sm_dcsctp PRIVATE -Wno-shadow -Wno-comment)
  endif()
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

if(SM_WITH_TLS AND SM_WITH_SYNC)
  # SRTP and DTLS-SRTP transports (C5 part 3, #1007): libwebrtc's pc/rtp_transport, pc/srtp_transport and
  # pc/dtls_srtp_transport on the DTLS transport (sm::p2p_dtls), SrtpSession (sm::tls) and the RtpDemuxer (sm::sync).
  # Not in the sm::sm umbrella yet, like sm::tls and sm::p2p (#944).
  sm_add_library(pc DEPS sm::p2p_dtls sm::sync SOURCES
    pc/dtls_srtp_transport.cc
    pc/rtp_transport.cc
    pc/srtp_transport.cc)
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

if(SM_WITH_TLS AND SM_WITH_SYNC AND SM_WITH_RTP)
  # The endpoint that sets up a call from SDP (C6 part 3, #1064): libwebrtc's pc/jsep_transport_controller and
  # pc/jsep_transport (ICE, DTLS and DTLS-SRTP per BUNDLE group) and the transport half of pc/sdp_offer_answer, with
  # the RTP session (sm::rtp) as the media channels. Not in the sm::sm umbrella yet, like sm::pc (#944).
  sm_add_library(pc_endpoint DEPS sm::pc sm::pc_sdp sm::rtp SOURCES
    pc/jsep_transport_controller.cc
    pc/peer_endpoint.cc)
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
