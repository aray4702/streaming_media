add_library(sm_test_framework STATIC framework/sm_test_main.cc)
target_include_directories(sm_test_framework PUBLIC framework)
sm_configure_target(sm_test_framework)

# One executable per section; one CTest entry per suite (= per algorithm).
if(SM_WITH_TLS)
  sm_add_test_exe(sm_unit_tls LABEL unit DEPS sm::tls
    SOURCES unit/tls/message_digest_test.cc
            unit/tls/ssl_identity_test.cc
            unit/tls/dtls_transport_test.cc
            unit/tls/srtp_session_test.cc
            unit/tls/tls_stream_test.cc
    SUITES TlsMessageDigest TlsCryptoRandom TlsSslIdentity TlsSslFingerprint TlsDtlsTransport TlsSrtpSession
           TlsDtlsSrtp TlsTlsStream)
  if(SM_TLS_CROSS_TEST)
    # The same unit tests on the other backend (sm::tls_mbedtls, or sm::tls_boringssl when sm::tls is on mbedTLS;
    # without SrtpSession), and the BoringSSL <-> mbedTLS cross test (#998).
    add_executable(sm_unit_tls_${SM_TLS_OTHER_BACKEND} unit/tls/message_digest_test.cc unit/tls/ssl_identity_test.cc
                   unit/tls/dtls_transport_test.cc unit/tls/tls_stream_test.cc)
    target_link_libraries(sm_unit_tls_${SM_TLS_OTHER_BACKEND} PRIVATE sm_test_framework sm::tls_${SM_TLS_OTHER_BACKEND})
    sm_configure_target(sm_unit_tls_${SM_TLS_OTHER_BACKEND})
    foreach(suite TlsMessageDigest TlsCryptoRandom TlsSslIdentity TlsSslFingerprint TlsDtlsTransport TlsTlsStream)
      add_test(NAME unit.${suite}.${SM_TLS_OTHER_BACKEND}
               COMMAND sm_unit_tls_${SM_TLS_OTHER_BACKEND} --filter=${suite}.)
      set_tests_properties(unit.${suite}.${SM_TLS_OTHER_BACKEND} PROPERTIES LABELS unit)
    endforeach()
    # One tls_cross::Peer per backend: cross_peer.cc compiled against each sm::tls build.
    foreach(backend boringssl mbedtls)
      if(backend STREQUAL SM_TLS_OTHER_BACKEND)
        set(lib sm::tls_${backend})
      else()
        set(lib sm::tls)
      endif()
      add_library(sm_tls_cross_peer_${backend} STATIC unit/tls/cross_peer.cc)
      target_link_libraries(sm_tls_cross_peer_${backend} PRIVATE ${lib})
      target_include_directories(sm_tls_cross_peer_${backend} PUBLIC ${PROJECT_SOURCE_DIR}/include)
      sm_configure_target(sm_tls_cross_peer_${backend})
    endforeach()
    sm_add_test_exe(sm_unit_tls_cross LABEL unit DEPS sm_tls_cross_peer_boringssl sm_tls_cross_peer_mbedtls sm::core
      SOURCES unit/tls/cross_test.cc
      SUITES TlsCrossBackend)
  endif()
endif()

sm_add_test_exe(sm_unit_p2p LABEL unit DEPS sm::p2p
  SOURCES
    unit/p2p/connection_test.cc
    unit/p2p/ice_compare_test.cc
    unit/p2p/ice_controller_test.cc
    unit/p2p/ice_lite_test.cc
    unit/p2p/p2p_transport_channel_multihomed_test.cc
    unit/p2p/p2p_transport_channel_ping_test.cc
    unit/p2p/p2p_transport_channel_test.cc
    unit/p2p/dtls_utils_test.cc
    unit/p2p/packet_socket_test.cc
    unit/p2p/port_allocator_test.cc
    unit/p2p/port_test.cc
    unit/p2p/stun_request_test.cc
    unit/p2p/stun_test.cc
    unit/p2p/tcp_socket_test.cc
    unit/p2p/test_crypto.cc
    unit/p2p/test_crypto_test.cc
    unit/p2p/turn_port_tcp_test.cc
    unit/p2p/turn_port_test.cc
  SUITES
    AsyncStunTcpSocket
    AsyncTcpSocket
    DtlsUtils
    EmulatedSockets
    EmulatedTcp
    P2pBasicIceController
    P2pBasicPortAllocator
    P2pCandidate
    P2pConnection
    P2pConnectionCall
    P2pIceCompare
    P2pIceConfig
    P2pIceCredentialsIterator
    P2pIceLite
    P2pIpAddress
    P2pNetwork
    P2pNetworkManager
    P2pPort
    P2pPortAllocator
    P2pSocketAddress
    P2pTransportChannel
    P2pTransportChannelMultihomed
    P2pTransportChannelPing
    P2pStunPort
    P2pTurnPort
    P2pTurnPortTcp
    P2pTurnRestCredentials
    P2pTurnServer
    P2pUdpPort
    P2pWrappingActiveIceController
    Stun
    StunRequest
    StunRequestReentrance
    StunServer
    StunTestCrypto
)
if(SM_WITH_TLS)
  # The STUN crypto hooks on BoringSSL's crypto (sm::tls, #984).
  target_sources(sm_unit_p2p PRIVATE unit/p2p/stun_tls_test.cc unit/p2p/ssl_adapter_test.cc
                                     unit/p2p/turn_port_tls_test.cc unit/p2p/ice_compare_tls_test.cc)
  target_link_libraries(sm_unit_p2p PRIVATE sm::p2p_tls)
  add_test(NAME unit.StunTls COMMAND sm_unit_p2p --filter=StunTls.)
  set_tests_properties(unit.StunTls PROPERTIES LABELS unit)
  # The TLS client socket over the emulated TCP (C4 part 3, #1035).
  add_test(NAME unit.P2pSslAdapter COMMAND sm_unit_p2p --filter=P2pSslAdapter.)
  set_tests_properties(unit.P2pSslAdapter PROPERTIES LABELS unit)
  # TURN over TLS (C4 part 4, #1036).
  add_test(NAME unit.P2pTurnPortTls COMMAND sm_unit_p2p --filter=P2pTurnPortTls.)
  set_tests_properties(unit.P2pTurnPortTls PROPERTIES LABELS unit)
  # The ICE comparison's TURN over TLS rows (#1055 part 2).
  add_test(NAME unit.P2pIceCompareTls COMMAND sm_unit_p2p --filter=P2pIceCompareTls.)
  set_tests_properties(unit.P2pIceCompareTls PROPERTIES LABELS unit)
  # The TURN client against a real coturn over OS sockets (C4 part 5, #1037); skipped without `turnserver`.
  if(NOT WIN32 AND NOT EMSCRIPTEN)
    add_executable(sm_interop_coturn interop/coturn_test.cc)
    target_link_libraries(sm_interop_coturn PRIVATE sm_test_framework sm_os_sockets)
    sm_configure_target(sm_interop_coturn)
    add_test(NAME interop.Coturn COMMAND sm_interop_coturn --filter=Coturn.)
    set_tests_properties(interop.Coturn PROPERTIES LABELS interop SKIP_REGULAR_EXPRESSION "SKIPPED: coturn not installed")
    add_subdirectory(interop/browser)
    # The web app and signaling service (C15, #1102).
    add_subdirectory(${PROJECT_SOURCE_DIR}/apps ${CMAKE_CURRENT_BINARY_DIR}/apps)
  endif()
  # DTLS over a fake ICE transport (C5 part 2, #1006).
  sm_add_test_exe(sm_unit_p2p_dtls LABEL unit DEPS sm::p2p_dtls
    SOURCES
      unit/p2p_dtls/dtls_transport_test.cc
    SUITES
      P2pDtlsRole
      P2pDtlsTransport
  )
  # The session description model and SDP parse/write (C6 part 1, #1062), offer/answer generation (C6 part 2, #1063).
  sm_add_test_exe(sm_unit_pc_sdp LABEL unit DEPS sm::pc_sdp
    SOURCES
      unit/pc_sdp/media_session_test.cc
      unit/pc_sdp/session_description_test.cc
      unit/pc_sdp/transport_description_factory_test.cc
      unit/pc_sdp/webrtc_sdp_test.cc
    SUITES
      PcCandidateParse
      PcJsepSessionDescription
      PcMediaContentDescription
      PcMediaSession
      PcSessionDescription
      PcSimulcastSdp
      PcTransportDescriptionFactory
      PcWebRtcSdp
  )
  if(SM_WITH_SYNC)
    # SRTP and DTLS-SRTP transports over the fake ICE transport (C5 part 3, #1007), and the connection stats over
    # the emulated network (C8, #1059).
    sm_add_test_exe(sm_unit_pc LABEL unit DEPS sm::pc
      SOURCES
        unit/p2p/test_crypto.cc
        unit/pc/connection_stats_test.cc
        unit/pc/dtls_srtp_transport_test.cc
        unit/pc/rtp_transport_test.cc
        unit/pc/srtp_transport_test.cc
      SUITES
        PcConnectionStats
        PcDtlsSrtpTransport
        PcRtpTransport
        PcRtpUtils
        PcSrtpTransport
    )
    if(SM_WITH_RTP)
      # The endpoint that sets up a call from SDP: ICE, DTLS-SRTP and the RTP session (C6 part 3, #1064).
      sm_add_test_exe(sm_unit_pc_endpoint LABEL unit DEPS sm::pc_endpoint sm::p2p_tls
        SOURCES
          unit/pc_endpoint/peer_endpoint_test.cc
        SUITES
          PcJsepTransportController
          PcPeerEndpoint
      )
    endif()
  endif()
endif()

if(SM_WITH_DATACHANNEL)
  # dcsctp's packet layer and common/ (C7 part 1, #1032): every *_test.cc under net/dcsctp/packet/ and common/ at
  # M157, on sm_test.h with gmock_lite.h.
  sm_add_test_exe(sm_unit_dcsctp LABEL unit DEPS sm::dcsctp
    SOURCES
      unit/dcsctp/common/math_test.cc
      unit/dcsctp/common/sequence_numbers_test.cc
      unit/dcsctp/packet/bounded_byte_reader_test.cc
      unit/dcsctp/packet/bounded_byte_writer_test.cc
      unit/dcsctp/packet/chunk/abort_chunk_test.cc
      unit/dcsctp/packet/chunk/cookie_ack_chunk_test.cc
      unit/dcsctp/packet/chunk/cookie_echo_chunk_test.cc
      unit/dcsctp/packet/chunk/data_chunk_test.cc
      unit/dcsctp/packet/chunk/error_chunk_test.cc
      unit/dcsctp/packet/chunk/forward_tsn_chunk_test.cc
      unit/dcsctp/packet/chunk/heartbeat_ack_chunk_test.cc
      unit/dcsctp/packet/chunk/heartbeat_request_chunk_test.cc
      unit/dcsctp/packet/chunk/idata_chunk_test.cc
      unit/dcsctp/packet/chunk/iforward_tsn_chunk_test.cc
      unit/dcsctp/packet/chunk/init_ack_chunk_test.cc
      unit/dcsctp/packet/chunk/init_chunk_test.cc
      unit/dcsctp/packet/chunk/reconfig_chunk_test.cc
      unit/dcsctp/packet/chunk/sack_chunk_test.cc
      unit/dcsctp/packet/chunk/shutdown_ack_chunk_test.cc
      unit/dcsctp/packet/chunk/shutdown_chunk_test.cc
      unit/dcsctp/packet/chunk/shutdown_complete_chunk_test.cc
      unit/dcsctp/packet/chunk_validators_test.cc
      unit/dcsctp/packet/crc32c_test.cc
      unit/dcsctp/packet/error_cause/cookie_received_while_shutting_down_cause_test.cc
      unit/dcsctp/packet/error_cause/invalid_mandatory_parameter_cause_test.cc
      unit/dcsctp/packet/error_cause/invalid_stream_identifier_cause_test.cc
      unit/dcsctp/packet/error_cause/missing_mandatory_parameter_cause_test.cc
      unit/dcsctp/packet/error_cause/no_user_data_cause_test.cc
      unit/dcsctp/packet/error_cause/out_of_resource_error_cause_test.cc
      unit/dcsctp/packet/error_cause/protocol_violation_cause_test.cc
      unit/dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause_test.cc
      unit/dcsctp/packet/error_cause/stale_cookie_error_cause_test.cc
      unit/dcsctp/packet/error_cause/unrecognized_chunk_type_cause_test.cc
      unit/dcsctp/packet/error_cause/unrecognized_parameter_cause_test.cc
      unit/dcsctp/packet/error_cause/unresolvable_address_cause_test.cc
      unit/dcsctp/packet/error_cause/user_initiated_abort_cause_test.cc
      unit/dcsctp/packet/parameter/add_incoming_streams_request_parameter_test.cc
      unit/dcsctp/packet/parameter/add_outgoing_streams_request_parameter_test.cc
      unit/dcsctp/packet/parameter/forward_tsn_supported_parameter_test.cc
      unit/dcsctp/packet/parameter/incoming_ssn_reset_request_parameter_test.cc
      unit/dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter_test.cc
      unit/dcsctp/packet/parameter/parameter_test.cc
      unit/dcsctp/packet/parameter/reconfiguration_response_parameter_test.cc
      unit/dcsctp/packet/parameter/ssn_tsn_reset_request_parameter_test.cc
      unit/dcsctp/packet/parameter/state_cookie_parameter_test.cc
      unit/dcsctp/packet/parameter/supported_extensions_parameter_test.cc
      unit/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter_test.cc
      unit/dcsctp/packet/sctp_packet_test.cc
      unit/dcsctp/packet/tlv_trait_test.cc
    SUITES
      DcsctpAbortChunkTest
      DcsctpAddIncomingStreamsRequestParameterTest
      DcsctpAddOutgoingStreamsRequestParameterTest
      DcsctpBoundedByteReaderTest
      DcsctpBoundedByteWriterTest
      DcsctpChunkValidatorsTest
      DcsctpCookieAckChunkTest
      DcsctpCookieEchoChunkTest
      DcsctpCookieReceivedWhileShuttingDownCauseTest
      DcsctpCrc32Test
      DcsctpDataChunkTest
      DcsctpErrorChunkTest
      DcsctpForwardTsnChunkTest
      DcsctpForwardTsnSupportedParameterTest
      DcsctpHeartbeatAckChunkTest
      DcsctpHeartbeatRequestChunkTest
      DcsctpIDataChunkTest
      DcsctpIForwardTsnChunkTest
      DcsctpIncomingSSNResetRequestParameterTest
      DcsctpInitAckChunkTest
      DcsctpInitChunkTest
      DcsctpInvalidMandatoryParameterCauseTest
      DcsctpInvalidStreamIdentifierCauseTest
      DcsctpMathUtilTest
      DcsctpMissingMandatoryParameterCauseTest
      DcsctpNoUserDataCauseTest
      DcsctpOutOfResourceErrorCauseTest
      DcsctpOutgoingSSNResetRequestParameterTest
      DcsctpParameterTest
      DcsctpProtocolViolationCauseTest
      DcsctpReConfigChunkTest
      DcsctpReconfigurationResponseParameterTest
      DcsctpRestartOfAnAssociationWithNewAddressesCauseTest
      DcsctpSSNTSNResetRequestParameterTest
      DcsctpSackChunkTest
      DcsctpSctpPacketTest
      DcsctpSequenceNumbersTest
      DcsctpShutdownAckChunkTest
      DcsctpShutdownChunkTest
      DcsctpShutdownCompleteChunkTest
      DcsctpStaleCookieErrorCauseTest
      DcsctpStateCookieParameterTest
      DcsctpSupportedExtensionsParameterTest
      DcsctpTlvDataTest
      DcsctpUnrecognizedChunkTypeCauseTest
      DcsctpUnrecognizedParametersCauseTest
      DcsctpUnresolvableAddressCauseTest
      DcsctpUserInitiatedAbortCauseTest
      DcsctpZeroChecksumAcceptableChunkParameterTest
  )
  target_include_directories(sm_unit_dcsctp PRIVATE unit/dcsctp)
  # dcsctp's socket, rx, tx and timer (C7 part 2, #1060): every *_test.cc under net/dcsctp/{public,socket,rx,tx,timer}/
  # at M157 except task_queue_timeout_test.cc (replaced by process_timeout_test.cc) and the network-emulation test, on
  # sm_test.h with gmock_lite.h's mocks.
  sm_add_test_exe(sm_unit_dcsctp_socket LABEL unit DEPS sm::dcsctp
    SOURCES
      unit/dcsctp/common/handover_testing.cc
      unit/dcsctp/public/types_test.cc
      unit/dcsctp/rx/data_tracker_test.cc
      unit/dcsctp/rx/interleaved_reassembly_streams_test.cc
      unit/dcsctp/rx/reassembly_queue_test.cc
      unit/dcsctp/rx/traditional_reassembly_streams_test.cc
      unit/dcsctp/socket/dcsctp_socket_test.cc
      unit/dcsctp/socket/heartbeat_handler_test.cc
      unit/dcsctp/socket/packet_sender_test.cc
      unit/dcsctp/socket/state_cookie_test.cc
      unit/dcsctp/socket/stream_reset_handler_test.cc
      unit/dcsctp/socket/transmission_control_block_test.cc
      unit/dcsctp/testing/data_generator.cc
      unit/dcsctp/timer/process_timeout_test.cc
      unit/dcsctp/timer/timer_test.cc
      unit/dcsctp/tx/outstanding_data_test.cc
      unit/dcsctp/tx/retransmission_error_counter_test.cc
      unit/dcsctp/tx/retransmission_queue_test.cc
      unit/dcsctp/tx/retransmission_timeout_test.cc
      unit/dcsctp/tx/rr_send_queue_test.cc
      unit/dcsctp/tx/stream_scheduler_test.cc
    SUITES
      DcsctpDataTrackerTest
      DcsctpDcSctpSocketParametrizedTest
      DcsctpDcSctpSocketTest
      DcsctpDisabledHeartbeatHandlerTest
      DcsctpHeartbeatHandlerTest
      DcsctpInterleavedReassemblyStreamsTest
      DcsctpOutstandingDataTest
      DcsctpPacketSenderTest
      DcsctpProcessTimeoutTest
      DcsctpRRSendQueueTest
      DcsctpReassemblyQueueTest
      DcsctpRetransmissionErrorCounterTest
      DcsctpRetransmissionQueueTest
      DcsctpRetransmissionTimeoutTest
      DcsctpStateCookieTest
      DcsctpStreamResetHandlerTest
      DcsctpStreamSchedulerTest
      DcsctpTimerManagerTest
      DcsctpTimerTest
      DcsctpTraditionalReassemblyStreamsTest
      DcsctpTransmissionControlBlockTest
      DcsctpTypesTest)
  target_include_directories(sm_unit_dcsctp_socket PRIVATE unit/dcsctp)
  if(NOT MSVC)
    target_compile_options(sm_unit_dcsctp_socket PRIVATE -Wno-shadow -Wno-comment)
  endif()
  if(NOT MSVC)
    target_compile_options(sm_unit_dcsctp PRIVATE -Wno-shadow)
  endif()
  if(SM_WITH_TLS)
    # SCTP over DTLS (C7 part 3, #1069): DcSctpTransport and SctpTransport, two endpoints over ICE + DTLS on the
    # emulated network.
    sm_add_test_exe(sm_unit_sctp LABEL unit DEPS sm::sctp sm::p2p_tls
      SOURCES
        unit/sctp/dcsctp_transport_test.cc
      SUITES
        SctpDcSctpTransport
        SctpOverDtls
    )
    # DCEP and data channels (C7 part 4, #1070): pc/sctp_utils_unittest, the channel against a fake controller,
    # and two endpoints opening channels both ways over ICE + DTLS + SCTP on the emulated network.
    sm_add_test_exe(sm_unit_pc_datachannel LABEL unit DEPS sm::pc_datachannel sm::p2p_tls
      SOURCES
        unit/pc_datachannel/sctp_utils_test.cc
        unit/pc_datachannel/data_channel_test.cc
      SUITES
        PcSctpUtils
        PcSctpDataChannel
        PcDataChannels
    )
  endif()
endif()

sm_add_test_exe(sm_unit_core LABEL unit DEPS sm::core
  SOURCES
    unit/core/audio_frame_operations_test.cc
    unit/core/core_test.cc
    unit/core/event_based_exponential_moving_average_test.cc
    unit/core/logging_test.cc
    unit/core/network_test.cc
  SUITES
    AudioFrameOperations
    Dsp
    EventBasedExponentialMovingAverage
    EventLog
    Fft
    Histogram
    Logging
    Metrics
    Nat
    NetworkLink
    Observability
    RateStatistics
    RateTracker
    RtpPacket
    SeqNum
    SimNetwork
    Units
)

if(SM_WITH_LOSS)
  sm_add_test_exe(sm_unit_loss LABEL unit DEPS sm::loss
    SOURCES
      unit/loss/audio_nack_tracker_test.cc
      unit/loss/fec_controller_test.cc
      unit/loss/keyframe_test.cc
      unit/loss/nack_rtx_test.cc
      unit/loss/red_test.cc
      unit/loss/rtcp_feedback_test.cc
      unit/loss/ulpfec_test.cc
    SUITES
      AudioNackTracker
      FecController
      KeyframeRequest
      NackTracker
      Red
      RtcpFeedback
      Rtx
      Ulpfec
  )
  sm_add_test_exe(sm_e2e_loss LABEL e2e DEPS sm::loss
    SOURCES
      e2e/loss_e2e_test.cc
    SUITES
      E2E_Fec
      E2E_Keyframe
      E2E_NackRtx
      E2E_Protection
      E2E_Red
  )
endif()

if(SM_WITH_TIMING)
  include(${CMAKE_CURRENT_SOURCE_DIR}/timing.cmake)
endif()
if(SM_WITH_CC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cc.cmake)
endif()
if(SM_WITH_AUDIO)
  include(${CMAKE_CURRENT_SOURCE_DIR}/audio.cmake)
endif()
if(SM_WITH_VIDEO)
  include(${CMAKE_CURRENT_SOURCE_DIR}/video.cmake)
endif()
if(SM_WITH_SYNC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/sync.cmake)
endif()
if(TARGET sm_device)
  include(${CMAKE_CURRENT_SOURCE_DIR}/device.cmake)
endif()
# The browser binding's native code (web/), without Emscripten.
if(SM_WITH_TIMING AND SM_WITH_AUDIO)
  sm_add_test_exe(sm_unit_web LABEL unit DEPS sm::timing sm::audio
    SOURCES
      unit/web/audio_jitter_buffer_binding_test.cc
    SUITES
      WebAudioJitterBuffer
  )
  target_include_directories(sm_unit_web PRIVATE ${PROJECT_SOURCE_DIR}/web)
endif()
if(SM_WITH_RTP)
  include(${CMAKE_CURRENT_SOURCE_DIR}/rtp.cmake)
endif()
